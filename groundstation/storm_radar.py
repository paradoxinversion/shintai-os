"""Enrai stormscope — the signature motion-tracker (docs/style.md §5.1), for lightning.

Reads the newest Enrai strike log (groundstation/logs/lightning-<date>.csv, written by
tools/lightning-logger.py: wall_time, board_ms, distance_km, energy) and builds a
self-contained, offline stormscope: a bottom-anchored Aliens-style radar fan with a
rotating sweep, concentric range rings, and every strike painting as a blip that fades
with CRT persistence — overhead in ALERT red (blinking), mid-range amber, distant phosphor.

Physical honesty (style.md §1.5): the AS3935 senses RANGE, not bearing — so blips sit at
their true range and azimuth is marked indicative, never invented.

Run:  conda activate shintai && python groundstation/storm_radar.py [logfile-substring]
Out:  analysis/storm.html  (self-contained, offline)  -> opened
"""
import os
import sys
import glob
import json
import datetime
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")
OUT = os.path.join(HERE, "analysis", "storm.html")


def newest_strike_log(arg):
    logs = sorted(glob.glob(os.path.join(LOG_DIR, "lightning-*.csv")),
                  key=os.path.getmtime, reverse=True)
    if arg:
        logs = [p for p in logs if arg in os.path.basename(p)] or logs
    if not logs:
        raise SystemExit("No lightning-*.csv strike log in %s (run tools/lightning-logger.py)" % LOG_DIR)
    return logs[0]


def parse_strikes(path):
    """-> (strikes, meta). Each strike: km, energy, r (0..1 range), cls, f (0..1 azimuth)."""
    rows = []
    with open(path) as fh:
        header = fh.readline().strip().split(",")
        idx = {name: i for i, name in enumerate(header)}
        for line in fh:
            c = line.rstrip("\n").split(",")
            if len(c) < len(header):
                continue
            rows.append(c)

    def cell(c, name):
        return c[idx[name]] if name in idx and idx[name] < len(c) else ""

    strikes = []
    for i, c in enumerate(rows):
        dk = cell(c, "distance_km").strip()
        try:
            energy = int(cell(c, "energy") or 0)
        except ValueError:
            energy = 0
        # Range → radius fraction (origin = wearer at bottom; outward = farther).
        if dk == "overhead":
            km, r, cls = 0, 0.16, "overhead"
        elif dk in ("out_of_range", "oor", ""):
            km, r, cls = -1, 1.0, "oor"
        else:
            try:
                km = int(float(dk))
            except ValueError:
                km = -1
            if km < 0:
                r, cls = 1.0, "oor"
            elif km <= 15:
                r, cls = 0.16 + (km / 15.0) * 0.52, "near"
            else:
                r, cls = 0.68 + (min(km, 40) - 15) / 25.0 * 0.30, "far"
        # Azimuth is NOT sensed — spread deterministically (golden angle) across the fan.
        f = (i * 0.6180339887) % 1.0
        strikes.append({"km": km, "e": energy, "r": round(r, 4), "cls": cls, "f": round(f, 4)})

    total = len(strikes)
    overhead = sum(1 for s in strikes if s["cls"] == "overhead")
    # rate: strikes per minute over the wall-clock span of the log
    rate = None
    try:
        fmt = "%Y-%m-%d %H:%M:%S"
        ts = [datetime.datetime.strptime(c[idx["wall_time"]].strip(), fmt)
              for c in rows if c[idx["wall_time"]].strip()]
        span_min = (ts[-1] - ts[0]).total_seconds() / 60.0 if len(ts) >= 2 else 0
        if span_min > 0:
            rate = round(total / span_min, 1)
    except (KeyError, ValueError):
        pass
    nearest = "OVERHEAD" if overhead else (
        "%d KM" % min([s["km"] for s in strikes if s["km"] > 0], default=0)
        if any(s["km"] > 0 for s in strikes) else "— —")
    last_e = strikes[-1]["e"] if strikes else 0
    meta = {"log": os.path.basename(path), "total": total, "overhead": overhead,
            "rate": rate, "nearest": nearest, "last_e": last_e}
    return strikes, meta


TEMPLATE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ENRAI STORMSCOPE</title>
<style>
:root{
  --void:#05080A; --panel:#0C1410; --grid:#1C4028;
  --phosphor:#58F07A; --phosphor-dim:#2E7A45;
  --amber:#F2A93B; --alert:#FF4438;
  --bone:#C9CDBC; --bone-dim:#6B6F62;
  --mono: ui-monospace,"IBM Plex Mono","SFMono-Regular",Menlo,Consolas,monospace;
}
*{box-sizing:border-box;}
html,body{margin:0;height:100%;}
body{
  background:var(--void); color:var(--bone); font-family:var(--mono);
  font-size:14px; line-height:1.4; overflow:hidden;
}
/* subtle CRT scanlines over everything (style.md §7) */
body::after{
  content:"";position:fixed;inset:0;pointer-events:none;z-index:50;
  background:repeating-linear-gradient(to bottom,rgba(0,0,0,0) 0 2px,rgba(0,0,0,0.16) 2px 3px);
  mix-blend-mode:multiply;
}
.console{display:grid;grid-template-rows:auto 1fr auto;height:100%;gap:0;}
/* header */
.hdr{display:flex;align-items:baseline;gap:16px;padding:12px 20px;border-bottom:1px solid var(--grid);}
.hdr .mark{color:var(--phosphor);letter-spacing:.22em;font-weight:600;}
.hdr .sub{color:var(--bone-dim);letter-spacing:.14em;font-size:12px;text-transform:uppercase;}
.hdr .log{margin-left:auto;color:var(--bone-dim);font-size:12px;letter-spacing:.1em;}
/* body: scope + readouts */
.main{display:grid;grid-template-columns:1fr 300px;min-height:0;}
.scopewrap{position:relative;min-height:0;display:flex;align-items:center;justify-content:center;padding:16px;}
/* reticle corner ticks (style.md §4) */
.scopewrap::before,.scopewrap::after,.tick-bl,.tick-br{position:absolute;width:22px;height:22px;pointer-events:none;}
.scopewrap::before{content:"";top:14px;left:14px;border-top:2px solid var(--phosphor-dim);border-left:2px solid var(--phosphor-dim);}
.scopewrap::after{content:"";top:14px;right:14px;border-top:2px solid var(--phosphor-dim);border-right:2px solid var(--phosphor-dim);}
.tick-bl{bottom:14px;left:14px;border-bottom:2px solid var(--phosphor-dim);border-left:2px solid var(--phosphor-dim);}
.tick-br{bottom:14px;right:14px;border-bottom:2px solid var(--phosphor-dim);border-right:2px solid var(--phosphor-dim);}
#scope{display:block;max-width:100%;max-height:100%;}
.honesty{position:absolute;bottom:22px;left:0;right:0;text-align:center;color:var(--bone-dim);font-size:11px;letter-spacing:.14em;}
/* readout column */
.readouts{border-left:1px solid var(--grid);padding:20px;display:flex;flex-direction:column;gap:20px;overflow-y:auto;}
.stat .lbl{color:var(--bone);font-size:11px;letter-spacing:.2em;text-transform:uppercase;}
.stat .val{color:var(--phosphor);font-size:56px;font-weight:600;line-height:1;font-variant-numeric:tabular-nums;margin-top:4px;}
.stat .val.small{font-size:26px;}
.stat .val.alert{color:var(--alert);}
.stat .unit{color:var(--bone-dim);font-size:13px;letter-spacing:.1em;}
.rows{display:flex;flex-direction:column;gap:9px;}
.row{display:flex;justify-content:space-between;align-items:baseline;gap:8px;border-bottom:1px dotted var(--grid);padding-bottom:6px;}
.row .k{color:var(--bone);font-size:12px;letter-spacing:.12em;text-transform:uppercase;}
.row .v{color:var(--phosphor);font-variant-numeric:tabular-nums;}
/* segmented storm-proximity meter (style.md §5.4) */
.meter .lbl{color:var(--bone);font-size:11px;letter-spacing:.2em;text-transform:uppercase;margin-bottom:6px;}
.segs{display:flex;gap:3px;}
.seg{flex:1;height:16px;background:var(--grid);}
.seg.on-far{background:var(--phosphor);}
.seg.on-mid{background:var(--amber);}
.seg.on-near{background:var(--alert);}
.meter .scale{display:flex;justify-content:space-between;color:var(--bone-dim);font-size:10px;letter-spacing:.1em;margin-top:5px;}
/* alert banner */
.banner{padding:10px 14px;border:1px solid var(--alert);color:var(--alert);font-weight:600;letter-spacing:.14em;display:none;}
.banner.show{display:block;animation:blink 1s steps(2,end) infinite;}
@keyframes blink{50%{opacity:.32;}}
/* strike ticker */
.ticker{border-top:1px solid var(--grid);padding:8px 20px;display:flex;gap:26px;overflow:hidden;white-space:nowrap;color:var(--bone-dim);font-size:12px;letter-spacing:.08em;}
.ticker b{color:var(--phosphor);font-weight:400;}
.ticker .oh{color:var(--alert);}
@media (prefers-reduced-motion:reduce){.banner.show{animation:none;}}
@media (max-width:820px){.main{grid-template-columns:1fr;}.readouts{border-left:0;border-top:1px solid var(--grid);flex-direction:row;flex-wrap:wrap;}}
</style>
</head>
<body>
<div class="console">
  <div class="hdr">
    <span class="mark">SHINTAI-OS</span>
    <span class="sub">// Enrai Stormscope</span>
    <span class="log">/*LOG*/</span>
  </div>
  <div class="main">
    <div class="scopewrap">
      <span class="tick-bl"></span><span class="tick-br"></span>
      <canvas id="scope"></canvas>
      <div class="honesty">RANGE ONLY · AS3935 SENSES DISTANCE, NOT BEARING · AZIMUTH INDICATIVE</div>
    </div>
    <div class="readouts">
      <div class="banner" id="banner">◆ STRIKE OVERHEAD — TAKE COVER</div>
      <div class="stat">
        <div class="lbl">Strikes logged</div>
        <div class="val" id="count">0</div>
      </div>
      <div class="rows">
        <div class="row"><span class="k">Nearest</span><span class="v" id="nearest">— —</span></div>
        <div class="row"><span class="k">Overhead</span><span class="v" id="overhead">0</span></div>
        <div class="row"><span class="k">Rate</span><span class="v" id="rate">— /min</span></div>
        <div class="row"><span class="k">Last energy</span><span class="v" id="laste">0</span></div>
      </div>
      <div class="meter">
        <div class="lbl">Storm proximity</div>
        <div class="segs" id="segs"></div>
        <div class="scale"><span>OVERHEAD</span><span>~40 KM</span></div>
      </div>
    </div>
  </div>
  <div class="ticker" id="ticker"></div>
</div>
<script>
let STRIKES = /*STRIKES*/;
let META = /*META*/;
const LIVE = /*LIVE*/;
const bornAt = {};        // strike index -> first-seen time (ms) for live phosphor fade
let sawCount = 0;
const fmt = n => (n||0).toLocaleString();

function applyData(){
  document.getElementById('count').textContent = META.total;
  document.getElementById('nearest').textContent = META.nearest;
  document.getElementById('overhead').textContent = META.overhead;
  document.getElementById('rate').textContent = (META.rate==null?'—':META.rate)+' /min';
  document.getElementById('laste').textContent = fmt(META.last_e);
  document.getElementById('banner').classList.toggle('show', META.overhead>0);
  // segmented proximity meter: the closest strike drives how many segments light + the band
  const segs=document.getElementById('segs'); const N=12;
  const minR = STRIKES.length?Math.min.apply(null,STRIKES.map(s=>s.r)):1;
  const lit = STRIKES.length?Math.max(1,Math.round((1-minR)*N)):0;
  const band = minR<0.22?'on-near':(minR<0.68?'on-mid':'on-far');
  segs.innerHTML='';
  for(let i=0;i<N;i++){const d=document.createElement('div');d.className='seg'+(i<lit?' '+band:'');segs.appendChild(d);}
  // ticker: last ~14 strikes, newest first
  const t=document.getElementById('ticker');
  t.innerHTML = STRIKES.slice(-14).reverse().map(s=>{
    const d = s.cls==='overhead'?'<span class="oh">OVERHEAD</span>':(s.cls==='oor'?'OUT OF RANGE':s.km+' KM');
    return '<span>⚡ '+d+' · <b>'+fmt(s.e)+'</b></span>';
  }).join('') || '<span>'+(LIVE?'AWAITING STRIKES…':'NO STRIKES LOGGED')+'</span>';
}

function markNew(list){                 // append-only log → index is a stable identity
  const now = performance.now();
  for(let i=sawCount;i<list.length;i++) bornAt[i]=now;
  sawCount = Math.max(sawCount, list.length);
}

async function refresh(){
  if(!LIVE) return;
  try{
    const j = await (await fetch('/strikes.json',{cache:'no-store'})).json();
    markNew(j.strikes); STRIKES=j.strikes; META=j.meta; applyData();
  }catch(e){ /* transient: keep the last frame */ }
}

markNew(STRIKES); applyData();
if(LIVE){ refresh(); setInterval(refresh, 2000); }

// --- radar scope (canvas) ---
const COL={grid:'#1C4028',dim:'#2E7A45',ph:'#58F07A',amber:'#F2A93B',alert:'#FF4438',bone:'#6B6F62'};
const cv=document.getElementById('scope'), ctx=cv.getContext('2d');
const dpr=Math.min(window.devicePixelRatio||1,2);
let W,H,cx,cy,R;
const reduce = matchMedia('(prefers-reduced-motion:reduce)').matches;
function size(){
  const wrap=cv.parentElement;
  const s=Math.max(200,Math.min(wrap.clientWidth-40,wrap.clientHeight-40));
  cv.style.width=s+'px'; cv.style.height=s+'px';
  cv.width=s*dpr; cv.height=s*dpr;
  W=cv.width;H=cv.height; cx=W/2; cy=H*0.9; R=H*0.8;
}
size(); addEventListener('resize',size);
// origin fan spans the upper semicircle: canvas angle π (left) .. 2π (right), through top.
const A0=Math.PI, A1=2*Math.PI, SPAN=A1-A0;
function pt(r,a){return [cx+r*Math.cos(a), cy+r*Math.sin(a)];}
const RINGS=[0.22,0.45,0.68,1.0];
const RING_LBL=['OVERHEAD','','','OUT OF RANGE'];
function frame(ts){
  ctx.clearRect(0,0,W,H);
  // rings
  ctx.lineWidth=1*dpr; ctx.font=(11*dpr)+'px ui-monospace,monospace';
  RINGS.forEach((rf,i)=>{
    ctx.strokeStyle=COL.grid; ctx.beginPath(); ctx.arc(cx,cy,R*rf,A0,A1); ctx.stroke();
    if(RING_LBL[i]){ctx.fillStyle=COL.bone; ctx.textAlign='center';
      ctx.fillText(RING_LBL[i], cx, cy-R*rf-4*dpr);}
  });
  // graduation ticks on the outer ring
  ctx.strokeStyle=COL.dim;
  for(let k=0;k<=12;k++){const a=A0+SPAN*k/12; const [x1,y1]=pt(R*0.97,a),[x2,y2]=pt(R,a);
    ctx.beginPath();ctx.moveTo(x1,y1);ctx.lineTo(x2,y2);ctx.stroke();}
  // radial spokes (faint)
  ctx.strokeStyle=COL.grid;
  for(let k=1;k<12;k+=2){const a=A0+SPAN*k/12;const [x,y]=pt(R,a);
    ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(x,y);ctx.globalAlpha=.5;ctx.stroke();ctx.globalAlpha=1;}
  // sweep (ping-pong across the fan)
  const period=5200; const p=reduce?0.5:((ts%period)/period);
  const tri=p<0.5?p*2:(1-(p-0.5)*2);            // 0..1..0
  const sweep=A0+0.03+tri*(SPAN-0.06);
  if(!reduce){
    // trailing wedge behind the sweep (phosphor persistence)
    ctx.save();ctx.beginPath();ctx.moveTo(cx,cy);
    const back=sweep-0.28*(p<0.5?1:-1);
    ctx.arc(cx,cy,R, Math.min(sweep,back), Math.max(sweep,back)); ctx.closePath();
    const g=ctx.createRadialGradient(cx,cy,0,cx,cy,R);
    g.addColorStop(0,'rgba(88,240,122,0.14)');g.addColorStop(1,'rgba(88,240,122,0)');
    ctx.fillStyle=g;ctx.fill();ctx.restore();
  }
  const [sx,sy]=pt(R,sweep);
  ctx.strokeStyle=COL.ph;ctx.lineWidth=1.6*dpr;ctx.globalAlpha=.9;
  ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(sx,sy);ctx.stroke();ctx.globalAlpha=1;
  // blips: paint brighter as the sweep passes, fade with persistence; overhead blinks
  STRIKES.forEach((s,i)=>{
    const a=A0+0.05+s.f*(SPAN-0.10);
    const [x,y]=pt(R*s.r,a);
    const base = s.cls==='overhead'?COL.alert:(s.cls==='near'?COL.amber:COL.ph);
    // proximity of the rotating sweep to this blip's angle -> paint boost
    let d=Math.abs(sweep-a); const boost=Math.max(0,1-d/0.5);
    let al = 0.24 + 0.5*boost;
    // live phosphor persistence: a freshly-arrived strike flares, then decays over ~12s
    const born=bornAt[i]; if(born!=null){const age=(ts-born)/1000; if(age<12) al += 0.7*(1-age/12);}
    if(s.cls==='overhead' && !reduce){al *= (0.55+0.45*Math.abs(Math.sin(ts/300)));}
    ctx.globalAlpha=Math.min(1,al);
    const rad=(s.cls==='overhead'?3.4:2.6)*dpr*(1+0.6*boost);
    ctx.fillStyle=base;
    ctx.shadowColor=base; ctx.shadowBlur=8*dpr*al;
    ctx.beginPath();ctx.arc(x,y,rad,0,7);ctx.fill();
    ctx.shadowBlur=0;ctx.globalAlpha=1;
  });
  // origin marker (the wearer)
  ctx.fillStyle=COL.ph;ctx.beginPath();ctx.arc(cx,cy,3*dpr,0,7);ctx.fill();
  ctx.strokeStyle=COL.dim;ctx.lineWidth=1*dpr;ctx.beginPath();ctx.arc(cx,cy,7*dpr,A0,A1);ctx.stroke();
  if(!reduce) requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
</script>
</body>
</html>
"""


def render_html(strikes, meta, live):
    return (TEMPLATE
            .replace("/*STRIKES*/", json.dumps(strikes))
            .replace("/*META*/", json.dumps(meta))
            .replace("/*LIVE*/", "true" if live else "false")
            .replace("/*LOG*/", meta["log"]))


def build(path):
    """Static snapshot → analysis/storm.html."""
    strikes, meta = parse_strikes(path)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        fh.write(render_html(strikes, meta, live=False))
    return meta


def serve(arg, port=8137):
    """Live stormscope — a local server that re-reads the newest strike log every poll,
    so strikes paint onto the scope as the logger writes them. Stdlib only."""
    import http.server
    import socketserver

    def snapshot():
        try:
            return parse_strikes(newest_strike_log(arg))
        except SystemExit:
            return [], {"log": "—", "total": 0, "overhead": 0,
                        "rate": None, "nearest": "— —", "last_e": 0}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, body, ctype):
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            strikes, meta = snapshot()
            if self.path.startswith("/strikes.json"):
                self._send(json.dumps({"strikes": strikes, "meta": meta}).encode(), "application/json")
            else:
                self._send(render_html(strikes, meta, live=True).encode(), "text/html; charset=utf-8")

    socketserver.ThreadingTCPServer.allow_reuse_address = True
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", port), Handler)
    srv.daemon_threads = True
    url = "http://127.0.0.1:%d/" % port
    print("live stormscope → %s   (Ctrl+C to stop)" % url)
    subprocess.run(["open", url], check=False)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
        srv.shutdown()


if __name__ == "__main__":
    argv = sys.argv[1:]
    live = "--serve" in argv or "--live" in argv
    rest = [a for a in argv if not a.startswith("-")]
    arg = rest[0] if rest else None
    if live:
        serve(arg)
    else:
        src = newest_strike_log(arg)
        print("strike log:", os.path.basename(src))
        m = build(src)
        print("strikes: %d  (%d overhead)  nearest %s  -> %s"
              % (m["total"], m["overhead"], m["nearest"], OUT))
        subprocess.run(["open", OUT], check=False)
