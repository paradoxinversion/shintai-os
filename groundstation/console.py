"""SHINTAI-OS live console — the Sulaco mission-control screen (docs/style.md §8).

The web posture the style guide describes: ONE live screen where the Enrai stormscope
(the signature motion-tracker, §5.1) sits alongside the other active sensor data — RANGE,
CLIMATE, THERMAL, and NAV as pulse-rifle segmented meters (§5.4) + readouts, with alert
banners (§5.7) and a strike ticker. A stdlib local server tails the newest telemetry
capture and pushes it to the browser; strikes are derived from the lightning_strikes count,
so the whole screen is one live capture.

Run:  conda activate shintai && python groundstation/console.py   (Ctrl+C to stop)
      python groundstation/console.py <logfile-substring>          (pick a capture)
Then open the printed http://127.0.0.1:… (it auto-opens).
"""
import os
import sys
import glob
import json
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")
PORT = 8138


def newest_log(arg):
    logs = sorted(glob.glob(os.path.join(LOG_DIR, "shintai_log_*.csv"))
                  + glob.glob(os.path.join(LOG_DIR, "shintai_ble_*.csv"))   # wireless BLE capture
                  + glob.glob(os.path.join(LOG_DIR, "spidey_log_*.csv")),
                  key=os.path.getmtime, reverse=True)
    if arg:
        logs = [p for p in logs if arg in os.path.basename(p)] or logs
    return logs[0] if logs else None


def _radius(dk):
    """distance_km value -> (km, radius 0..1, class)."""
    dk = (dk or "").strip()
    if dk in ("", "0"):
        return 0, 0.16, "overhead" if dk == "0" else "overhead"
    if dk in ("63", "out_of_range", "oor"):
        return -1, 1.0, "oor"
    try:
        km = int(float(dk))
    except ValueError:
        return -1, 1.0, "oor"
    if km <= 1:
        return km, 0.16, "overhead"
    if km <= 15:
        return km, 0.16 + (km / 15.0) * 0.52, "near"
    return km, 0.68 + (min(km, 40) - 15) / 25.0 * 0.30, "far"


# Kyūkaku (嗅覚) smell — mirrors :core Kyukaku.kt / KyukakuBand.h over the CSV's
# gas_ohms + humidity_pct: calibration-free, tracks an adaptive clean-air baseline R0 and
# works in the ratio r = gas/R0. A fast drop is a Spike; a sustained low r is Foul/Taint.
_KY = dict(TAINT_R=0.60, FOUL_R=0.35, HYST_R=0.05, SPIKE_DROP=0.25, HUM_VETO=3.0,
           SEED_ALPHA=0.10, BASE_UP=0.02, BASE_DOWN=0.0006, RREF_ALPHA=0.25,
           SETTLE=8, SPIKE_HOLD=2)


def _ky_step(r, prev):
    """Hysteretic band walk (mirror of Kyukaku.step), inverted for a falling signal."""
    if prev < 0 or prev > 2:
        return 2 if r < _KY["FOUL_R"] else (1 if r < _KY["TAINT_R"] else 0)
    edge, h, b = (_KY["TAINT_R"], _KY["FOUL_R"]), _KY["HYST_R"], prev
    while b < 2 and r < edge[b] - h:
        b += 1
    while b > 0 and r > edge[b - 1] + h:
        b -= 1
    return b


def kyukaku_smell(readings):
    """readings: [(gas_ohms|None, humidity|None)] oldest→newest → smell label, or None."""
    baseline = lastHum = 0.0
    rRef = ratio = 1.0
    band = count = spikeHold = 0
    for gas, hum in readings:
        if gas is None or gas <= 0:
            continue
        if hum is None:
            hum = lastHum
        if count == 0:
            baseline, rRef, lastHum, ratio, band, count, spikeHold = gas, 1.0, hum, 1.0, 0, 1, 0
            continue
        armed = count >= _KY["SETTLE"]
        base = (baseline + _KY["SEED_ALPHA"] * (gas - baseline) if not armed
                else baseline + (_KY["BASE_UP"] if gas > baseline else _KY["BASE_DOWN"]) * (gas - baseline))
        r = gas / base
        fresh = armed and (rRef - r) >= _KY["SPIKE_DROP"] and (hum - lastHum) < _KY["HUM_VETO"]
        band = _ky_step(r, band)
        baseline, rRef, lastHum, ratio = base, rRef + _KY["RREF_ALPHA"] * (r - rRef), hum, r
        count += 1
        spikeHold = _KY["SPIKE_HOLD"] if fresh else max(0, spikeHold - 1)
    if count == 0:
        return None
    if count < _KY["SETTLE"]:
        return "warming"
    if spikeHold > 0:
        return "spike"
    return {2: "foul", 1: "taint", 0: "clean"}[band]


def snapshot(path):
    """Latest telemetry row + strikes derived from the lightning_strikes count."""
    if not path or not os.path.exists(path):
        return {"row": {}, "strikes": [], "meta": {"samples": 0, "pod": "", "strikes": 0, "nearest": "—"}}
    with open(path) as fh:
        header = fh.readline().strip().split(",")
        rows = [ln.rstrip("\n").split(",") for ln in fh if ln[:1].isdigit()]
    idx = {n: i for i, n in enumerate(header)}

    def cell(r, name):
        return r[idx[name]] if name in idx and idx[name] < len(r) else ""

    latest = {n: cell(rows[-1], n) for n in header} if rows else {}

    # derive strikes: each row where lightning_strikes rose = a strike at that row's range.
    strikes, prev, si = [], None, 0
    for r in rows:
        ls = cell(r, "lightning_strikes").strip()
        if not ls.isdigit():
            continue
        n = int(ls)
        if prev is not None and n > prev:
            km, rad, cls = _radius(cell(r, "lightning_km"))
            try:
                e = int(cell(r, "lightning_energy") or 0)
            except ValueError:
                e = 0
            for _ in range(min(n - prev, 8)):     # cap a big jump between samples
                strikes.append({"km": km, "e": e, "r": round(rad, 4), "cls": cls,
                                "f": round((si * 0.6180339887) % 1.0, 4)})
                si += 1
        prev = n
    strikes = strikes[-80:]

    total = int(latest.get("lightning_strikes") or 0) if latest.get("lightning_strikes", "").isdigit() else len(strikes)
    overhead = sum(1 for s in strikes if s["cls"] == "overhead")
    nearest = "OVERHEAD" if overhead else (
        "%d KM" % min([s["km"] for s in strikes if s["km"] > 0], default=0)
        if any(s["km"] > 0 for s in strikes) else "—")

    def _f(r, name):
        try:
            return float(cell(r, name).strip())
        except ValueError:
            return None
    smell = kyukaku_smell([(_f(r, "gas_ohms"), _f(r, "humidity_pct")) for r in rows[-400:]])

    meta = {"samples": len(rows), "pod": latest.get("board", ""), "strikes": total,
            "overhead": overhead, "nearest": nearest, "smell": smell or "—",
            "log": os.path.basename(path)}
    return {"row": latest, "strikes": strikes, "meta": meta}


TEMPLATE = r"""<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>SHINTAI-OS CONSOLE</title>
<style>
:root{
  --void:#05080A;--panel:#0C1410;--grid:#1C4028;
  --phosphor:#58F07A;--phosphor-dim:#2E7A45;--amber:#F2A93B;--amber-dim:#7A5620;
  --alert:#FF4438;--alert-dim:#8A2820;--bone:#C9CDBC;--bone-dim:#6B6F62;
  --violet:#B36BFF;   /* Kyūkaku smell — a source identity, not a severity (per the apps) */
  --mono:ui-monospace,"IBM Plex Mono","SFMono-Regular",Menlo,Consolas,monospace;
}
.sm-clean{color:var(--phosphor);}.sm-taint{color:var(--amber);}.sm-foul{color:var(--alert);}
.sm-spike{color:var(--violet);font-weight:600;}.sm-warming,.sm-none{color:var(--bone-dim);}
*{box-sizing:border-box;}
html,body{margin:0;height:100%;}
body{background:var(--void);color:var(--bone);font-family:var(--mono);font-size:13px;overflow:hidden;}
body::after{content:"";position:fixed;inset:0;pointer-events:none;z-index:60;
  background:repeating-linear-gradient(to bottom,rgba(0,0,0,0) 0 2px,rgba(0,0,0,.16) 2px 3px);mix-blend-mode:multiply;}
.wrap{display:grid;grid-template-rows:auto 1fr;height:100%;}
.hdr{display:flex;align-items:baseline;gap:16px;padding:11px 18px;border-bottom:1px solid var(--grid);}
.hdr .mark{color:var(--phosphor);letter-spacing:.22em;font-weight:600;}
.hdr .sub{color:var(--bone-dim);letter-spacing:.14em;font-size:12px;text-transform:uppercase;}
.hdr .pod{color:var(--amber);letter-spacing:.14em;}
.hdr .n{margin-left:auto;color:var(--bone-dim);font-variant-numeric:tabular-nums;}
.main{display:grid;grid-template-columns:minmax(300px,360px) 1fr;gap:12px;padding:12px;min-height:0;}
.left{display:flex;flex-direction:column;gap:10px;min-height:0;overflow-y:auto;}
.right{display:grid;grid-template-columns:repeat(2,1fr);grid-auto-rows:min-content;gap:12px;min-height:0;overflow-y:auto;align-content:start;}
/* panel: reticle-tick framed, chamfer-free (border-radius:0) */
.panel{position:relative;background:var(--panel);border:1px solid var(--grid);padding:12px 14px;}
.panel>.t{color:var(--bone);font-size:11px;letter-spacing:.2em;text-transform:uppercase;margin-bottom:9px;}
.panel .rt{position:absolute;width:12px;height:12px;}
.rt.tl{top:-1px;left:-1px;border-top:2px solid var(--phosphor-dim);border-left:2px solid var(--phosphor-dim);}
.rt.br{bottom:-1px;right:-1px;border-bottom:2px solid var(--phosphor-dim);border-right:2px solid var(--phosphor-dim);}
.radarpanel{display:flex;flex-direction:column;}
.scopewrap{height:230px;position:relative;display:flex;align-items:center;justify-content:center;}
#scope{display:block;}
.honesty{position:absolute;bottom:2px;left:0;right:0;text-align:center;color:var(--bone-dim);font-size:10px;letter-spacing:.12em;}
.storows{display:flex;gap:22px;margin-top:6px;}
.storows .b{color:var(--bone-dim);font-size:11px;letter-spacing:.14em;text-transform:uppercase;}
.storows .b b{display:block;color:var(--phosphor);font-size:24px;font-weight:600;letter-spacing:0;font-variant-numeric:tabular-nums;}
.storows .b.alert b{color:var(--alert);}
/* meters */
.segs{display:flex;gap:3px;}
.seg{flex:1;height:14px;background:var(--grid);}
.seg.on-far{background:var(--phosphor);}.seg.dim-far{background:var(--phosphor-dim);}
.seg.on-mid{background:var(--amber);}.seg.dim-mid{background:var(--amber-dim);}
.seg.on-near{background:var(--alert);}.seg.dim-near{background:var(--alert-dim);}
.mval{margin-top:6px;font-size:18px;font-weight:600;font-variant-numeric:tabular-nums;color:var(--phosphor);}
.mval.mid{color:var(--amber);}.mval.near{color:var(--alert);}.mval.far{color:var(--phosphor);}
.sub2{color:var(--bone-dim);font-size:12px;margin-top:4px;letter-spacing:.06em;}
.rows{display:flex;flex-direction:column;gap:7px;}
.r{display:flex;justify-content:space-between;gap:8px;border-bottom:1px dotted var(--grid);padding-bottom:5px;}
.r .k{color:var(--bone);font-size:11px;letter-spacing:.12em;text-transform:uppercase;}
.r .v{color:var(--phosphor);font-variant-numeric:tabular-nums;}
/* banners */
.banner{padding:8px 12px;border:1px solid var(--alert);color:var(--alert);font-weight:600;letter-spacing:.14em;display:none;}
.banner.mid{border-color:var(--amber);color:var(--amber);}
.banner.show{display:block;animation:blink 1s steps(2,end) infinite;}
@keyframes blink{50%{opacity:.34;}}
.ticker{display:flex;flex-direction:column;gap:4px;max-height:150px;overflow-y:auto;color:var(--bone-dim);font-size:12px;letter-spacing:.05em;font-variant-numeric:tabular-nums;}
.ticker b{color:var(--phosphor);font-weight:400;}.ticker .oh{color:var(--alert);}
@media (prefers-reduced-motion:reduce){.banner.show{animation:none;}}
@media (max-width:900px){.main{grid-template-columns:1fr;}}
/* header gear + panel modal (§5.5 button, §5.6 status-LED toggles) */
.gear{margin-left:14px;background:none;border:1px solid var(--grid);color:var(--bone);font-family:var(--mono);
  font-size:11px;letter-spacing:.14em;padding:4px 10px;cursor:pointer;text-transform:uppercase;}
.gear:hover{border-color:var(--phosphor);color:var(--phosphor);}
.gear:focus-visible{outline:2px solid var(--phosphor);outline-offset:2px;}
.modal[hidden]{display:none;}
.modal{position:fixed;inset:0;z-index:100;background:rgba(5,8,10,.72);display:flex;align-items:center;justify-content:center;}
.modal-panel{position:relative;background:var(--panel);border:1px solid var(--grid);padding:16px 18px;min-width:280px;}
.modal-hdr{display:flex;align-items:center;justify-content:space-between;gap:24px;margin-bottom:12px;}
.modal-hdr .t{color:var(--bone);font-size:12px;letter-spacing:.2em;text-transform:uppercase;}
.modal-hdr .x{background:none;border:none;color:var(--bone-dim);font-family:var(--mono);font-size:14px;cursor:pointer;}
.modal-hdr .x:hover{color:var(--alert);}
.toggles{display:flex;flex-direction:column;gap:1px;}
.toggle{display:flex;align-items:center;gap:11px;background:none;border:none;color:var(--bone);font-family:var(--mono);
  font-size:13px;letter-spacing:.06em;padding:7px 6px;cursor:pointer;text-align:left;text-transform:uppercase;}
.toggle:hover{background:rgba(88,240,122,.06);}
.toggle:focus-visible{outline:1px solid var(--phosphor);outline-offset:-1px;}
.toggle .led{width:9px;height:9px;flex:none;border:1px solid var(--phosphor-dim);}
.toggle.on .led{background:var(--phosphor);border-color:var(--phosphor);}
.toggle.on{color:var(--phosphor);}
.modal-hint{margin-top:12px;color:var(--bone-dim);font-size:11px;letter-spacing:.06em;}
</style></head>
<body>
<div class="wrap">
  <div class="hdr"><span class="mark">SHINTAI-OS</span><span class="sub">// Live Console</span>
    <span class="pod" id="pod"></span><span class="n" id="samples">#0</span>
    <button class="gear" id="gear" title="Show / hide panels">⚙ PANELS</button></div>
  <div class="main">
    <div class="left">
      <div class="banner" id="b-hold">◆ OBJECT INSIDE 0.2 M — HOLD</div>
      <div class="banner mid" id="b-vent">◆ CO2 OVER LIMIT — VENTILATE</div>
      <div class="panel radarpanel" data-panel="stormscope" data-label="Stormscope"><span class="rt tl"></span><span class="rt br"></span>
        <div class="t">Enrai Stormscope</div>
        <div class="scopewrap"><canvas id="scope"></canvas>
          <div class="honesty">RANGE ONLY · AZIMUTH INDICATIVE</div></div>
        <div class="storows">
          <span class="b alert">STRIKES<b id="storm-count">0</b></span>
          <span class="b alert">NEAREST<b id="storm-near">—</b></span>
        </div>
      </div>
      <div class="panel" data-panel="strikes" data-label="Recent strikes"><span class="rt tl"></span><span class="rt br"></span>
        <div class="t">Recent strikes</div>
        <div class="ticker" id="ticker"></div>
      </div>
    </div>
    <div class="right">
      <div class="panel" data-panel="range" data-label="Range"><span class="rt tl"></span><span class="rt br"></span>
        <div class="t">Range · rear field</div><div class="segs" id="range-segs"></div>
        <div class="mval" id="range-val">—</div></div>
      <div class="panel" data-panel="climate" data-label="Climate"><span class="rt tl"></span><span class="rt br"></span>
        <div class="t">Climate · CO₂</div><div class="segs" id="co2-segs"></div>
        <div class="mval" id="co2-val">—</div><div class="sub2" id="climate-tr">—</div></div>
      <div class="panel" data-panel="thermal" data-label="Thermal"><span class="rt tl"></span><span class="rt br"></span>
        <div class="t">Thermal · hotspot Δ</div><div class="segs" id="therm-segs"></div>
        <div class="mval" id="therm-val">—</div><div class="sub2" id="therm-ctr">—</div></div>
      <div class="panel" data-panel="air" data-label="Air"><span class="rt tl"></span><span class="rt br"></span>
        <div class="t">Air · pressure / gas</div><div class="rows">
          <div class="r"><span class="k">Pressure</span><span class="v" id="air-press">—</span></div>
          <div class="r"><span class="k">Gas · VOC</span><span class="v" id="air-gas">—</span></div>
          <div class="r"><span class="k">Smell · Kyūkaku</span><span class="v" id="air-smell">—</span></div>
        </div></div>
      <div class="panel" data-panel="nav" data-label="Navigation"><span class="rt tl"></span><span class="rt br"></span>
        <div class="t">Navigation</div><div class="rows">
          <div class="r"><span class="k">Heading</span><span class="v" id="nav-hd">—</span></div>
          <div class="r"><span class="k">GPS</span><span class="v" id="nav-gps">—</span></div>
          <div class="r"><span class="k">Alt · Sats</span><span class="v" id="nav-alt">—</span></div>
          <div class="r"><span class="k">Accel</span><span class="v" id="nav-acc">—</span></div>
          <div class="r"><span class="k">Steps</span><span class="v" id="nav-steps">—</span></div>
        </div></div>
    </div>
  </div>
</div>
<div class="modal" id="modal" hidden>
  <div class="modal-panel"><span class="rt tl"></span><span class="rt br"></span>
    <div class="modal-hdr"><span class="t">Display · panels</span><button class="x" id="modal-x" title="Close">✕</button></div>
    <div class="toggles" id="toggles"></div>
    <div class="modal-hint">Toggle a panel to hide it. Saved on this browser.</div>
  </div>
</div>
<script>
let DATA=/*DATA*/; const LIVE=/*LIVE*/;
let STRIKES=DATA.strikes||[]; const bornAt={}; let sawCount=0;
const $=id=>document.getElementById(id);
const fmt=n=>(n==null||n==='')?'—':(+n).toLocaleString();
function mk(id){const s=$(id);if(!s.children.length)for(let i=0;i<12;i++){const d=document.createElement('div');d.className='seg';s.appendChild(d);}return s;}
function setMeter(id,frac,band,val){const s=mk(id+'-segs');frac=Math.max(0,Math.min(1,frac||0));const on=Math.round(frac*12);
  [...s.children].forEach((d,i)=>d.className='seg '+(i<on?'on-':'dim-')+band);
  const v=$(id+'-val');v.textContent=val;v.className='mval '+band;}
function num(r,k){const v=r[k];return(v===''||v==null)?null:+v;}
function apply(d){
  const r=d.row||{},m=d.meta||{};
  $('pod').textContent=m.pod?('['+m.pod+']'):''; $('samples').textContent='#'+(m.samples||0);
  const alertOn=r.alert==='1',co2=num(r,'co2_ppm');
  $('b-hold').classList.toggle('show',alertOn);
  $('b-vent').classList.toggle('show',co2!=null&&co2>=1500);
  const dl=num(r,'distance_l_mm'),dr=num(r,'distance_r_mm'),ds=[dl,dr].filter(x=>x!=null);
  if(ds.length){const dd=Math.min(...ds);setMeter('range',(3000-dd)/2800,dd<=200?'near':(dd<=1000?'mid':'far'),(dd/1000).toFixed(2)+' M');}
  else setMeter('range',0,'far','no reading');
  if(co2!=null)setMeter('co2',(co2-400)/1600,co2>=1500?'near':(co2>=1000?'mid':'far'),co2+' PPM');
  else setMeter('co2',0,'far','warming…');
  const at=num(r,'air_temp_c');$('climate-tr').textContent=at!=null?at.toFixed(1)+'°C  '+(num(r,'humidity_pct')||0)+'%RH':'—';
  const hs=num(r,'hotspot_delta');
  if(hs!=null)setMeter('therm',hs/20,hs>=10?'near':(hs>=5?'mid':'far'),(hs>=0?'+':'')+hs.toFixed(1)+'°C');
  else setMeter('therm',0,'far','—');
  const tc=num(r,'thermal_ctr'),tmin=num(r,'thermal_min'),tmax=num(r,'thermal_max');
  $('therm-ctr').textContent=tc!=null?(tc.toFixed(1)+'°C ctr'+(tmin!=null&&tmax!=null?'  ·  '+tmin.toFixed(0)+'–'+tmax.toFixed(0)+'°C':'')):'—';
  // AIR: pressure + gas + Kyūkaku smell (derived server-side from the gas baseline)
  const pr=num(r,'pressure_hpa'),ga=num(r,'gas_ohms'),sm=(m.smell||'—');
  $('air-press').textContent=pr!=null?pr.toFixed(1)+' hPa':'—';
  $('air-gas').textContent=ga!=null?(ga/1000).toFixed(0)+' kΩ':'—';
  const smEl=$('air-smell');smEl.textContent=sm==='—'?'—':sm.toUpperCase();
  smEl.className='v sm-'+(sm==='—'?'none':sm);
  // NAV
  const hd=num(r,'heading_deg');$('nav-hd').textContent=hd!=null?hd.toFixed(0)+'° '+(r.cardinal||''):'—';
  $('nav-gps').textContent=r.gps_fix==='1'?(num(r,'lat').toFixed(4)+','+num(r,'lon').toFixed(4)+'  '+(num(r,'speed_kmh')||0)+'km/h'):'no fix';
  const alt=num(r,'alt_m'),sat=num(r,'sats');
  $('nav-alt').textContent=r.gps_fix==='1'?((alt!=null?alt.toFixed(0)+'m':'—')+' · '+(sat!=null?sat+' sats':'—')):'—';
  const axx=num(r,'accel_x');$('nav-acc').textContent=axx!=null?'X'+axx.toFixed(1)+' Y'+num(r,'accel_y').toFixed(1)+' Z'+num(r,'accel_z').toFixed(1):'—';
  $('nav-steps').textContent=r.steps||'—';
  $('storm-count').textContent=m.strikes||0; $('storm-near').textContent=m.nearest||'—';
  $('ticker').innerHTML=STRIKES.slice(-14).reverse().map(s=>{
    const dd=s.cls==='overhead'?'<span class="oh">OVERHEAD</span>':(s.cls==='oor'?'OUT OF RANGE':s.km+' KM');
    return '<span>⚡ '+dd+' · <b>'+fmt(s.e)+'</b></span>';}).join('')||'<span>AWAITING STRIKES…</span>';
}
function markNew(l){const now=performance.now();for(let i=sawCount;i<l.length;i++)bornAt[i]=now;sawCount=Math.max(sawCount,l.length);}
async function poll(){if(!LIVE)return;try{const j=await(await fetch('/data.json',{cache:'no-store'})).json();markNew(j.strikes||[]);STRIKES=j.strikes||[];DATA=j;apply(j);}catch(e){}}
markNew(STRIKES);apply(DATA);if(LIVE){poll();setInterval(poll,1500);}
// --- radar ---
const COL={grid:'#1C4028',dim:'#2E7A45',ph:'#58F07A',amber:'#F2A93B',alert:'#FF4438',bone:'#6B6F62'};
const cv=$('scope'),ctx=cv.getContext('2d'),dpr=Math.min(devicePixelRatio||1,2);
const reduce=matchMedia('(prefers-reduced-motion:reduce)').matches;
let W,H,cx,cy,R;
function size(){const w=cv.parentElement,s=w.clientWidth,ht=w.clientHeight;
  cv.style.width=s+'px';cv.style.height=ht+'px';cv.width=s*dpr;cv.height=ht*dpr;
  W=cv.width;H=cv.height;cx=W/2;cy=H*0.92;R=H*0.84;}
size();addEventListener('resize',size);
const A0=Math.PI,A1=2*Math.PI,SPAN=A1-A0;
const pt=(r,a)=>[cx+r*Math.cos(a),cy+r*Math.sin(a)];
const RINGS=[0.22,0.45,0.68,1.0],LBL=['OVERHEAD','','','OUT OF RANGE'];
function frame(ts){
  ctx.clearRect(0,0,W,H);ctx.lineWidth=1*dpr;ctx.font=(10*dpr)+'px ui-monospace,monospace';
  RINGS.forEach((rf,i)=>{ctx.strokeStyle=COL.grid;ctx.beginPath();ctx.arc(cx,cy,R*rf,A0,A1);ctx.stroke();
    if(LBL[i]){ctx.fillStyle=COL.bone;ctx.textAlign='center';ctx.fillText(LBL[i],cx,cy-R*rf-3*dpr);}});
  ctx.strokeStyle=COL.dim;for(let k=0;k<=12;k++){const a=A0+SPAN*k/12,[x1,y1]=pt(R*0.97,a),[x2,y2]=pt(R,a);
    ctx.beginPath();ctx.moveTo(x1,y1);ctx.lineTo(x2,y2);ctx.stroke();}
  const period=5200,p=reduce?0.5:(ts%period)/period,tri=p<0.5?p*2:1-(p-0.5)*2,sweep=A0+0.03+tri*(SPAN-0.06);
  if(!reduce){ctx.save();ctx.beginPath();ctx.moveTo(cx,cy);const bk=sweep-0.28*(p<0.5?1:-1);
    ctx.arc(cx,cy,R,Math.min(sweep,bk),Math.max(sweep,bk));ctx.closePath();
    const g=ctx.createRadialGradient(cx,cy,0,cx,cy,R);g.addColorStop(0,'rgba(88,240,122,.13)');g.addColorStop(1,'rgba(88,240,122,0)');
    ctx.fillStyle=g;ctx.fill();ctx.restore();}
  const[sx,sy]=pt(R,sweep);ctx.strokeStyle=COL.ph;ctx.lineWidth=1.5*dpr;ctx.globalAlpha=.9;
  ctx.beginPath();ctx.moveTo(cx,cy);ctx.lineTo(sx,sy);ctx.stroke();ctx.globalAlpha=1;
  STRIKES.forEach((s,i)=>{const a=A0+0.05+s.f*(SPAN-0.10),[x,y]=pt(R*s.r,a);
    const base=s.cls==='overhead'?COL.alert:(s.cls==='near'?COL.amber:COL.ph);
    const boost=Math.max(0,1-Math.abs(sweep-a)/0.5);let al=.24+.5*boost;
    const bn=bornAt[i];if(bn!=null){const age=(ts-bn)/1000;if(age<12)al+=.7*(1-age/12);}
    if(s.cls==='overhead'&&!reduce)al*=(.55+.45*Math.abs(Math.sin(ts/300)));
    ctx.globalAlpha=Math.min(1,al);ctx.fillStyle=base;ctx.shadowColor=base;ctx.shadowBlur=7*dpr*al;
    ctx.beginPath();ctx.arc(x,y,(s.cls==='overhead'?3.2:2.5)*dpr*(1+.6*boost),0,7);ctx.fill();
    ctx.shadowBlur=0;ctx.globalAlpha=1;});
  ctx.fillStyle=COL.ph;ctx.beginPath();ctx.arc(cx,cy,3*dpr,0,7);ctx.fill();
  if(!reduce)requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
// --- panel show/hide modal (localStorage-persisted display preference; §5.6 LED toggles) ---
const PANELS=[...document.querySelectorAll('[data-panel]')].map(el=>({id:el.dataset.panel,label:el.dataset.label,el}));
const PKEY='shintai-console-panels';
let panelOn={};try{panelOn=JSON.parse(localStorage.getItem(PKEY))||{};}catch(e){}
const isOn=id=>panelOn[id]!==false;
function applyPanels(){PANELS.forEach(p=>{p.el.style.display=isOn(p.id)?'':'none';});}
function renderToggles(){const c=$('toggles');c.innerHTML='';PANELS.forEach(p=>{
  const b=document.createElement('button');b.className='toggle'+(isOn(p.id)?' on':'');
  b.setAttribute('aria-pressed',isOn(p.id));
  b.innerHTML='<span class="led"></span><span>'+p.label+'</span>';
  b.onclick=()=>{panelOn[p.id]=!isOn(p.id);localStorage.setItem(PKEY,JSON.stringify(panelOn));
    applyPanels();renderToggles();dispatchEvent(new Event('resize'));};
  c.appendChild(b);});}
$('gear').onclick=()=>{$('modal').hidden=false;};
$('modal-x').onclick=()=>{$('modal').hidden=true;};
$('modal').onclick=e=>{if(e.target===$('modal'))$('modal').hidden=true;};
addEventListener('keydown',e=>{if(e.key==='Escape')$('modal').hidden=true;});
applyPanels();renderToggles();
</script></body></html>
"""


def render(data, live):
    return (TEMPLATE.replace("/*DATA*/", json.dumps(data)).replace("/*LIVE*/", "true" if live else "false"))


def serve(arg, port=PORT):
    import http.server
    import socketserver

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
            data = snapshot(newest_log(arg))
            if self.path.startswith("/data.json"):
                self._send(json.dumps(data).encode(), "application/json")
            else:
                self._send(render(data, live=True).encode(), "text/html; charset=utf-8")

    socketserver.ThreadingTCPServer.allow_reuse_address = True
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", port), Handler)
    srv.daemon_threads = True
    url = "http://127.0.0.1:%d/" % port
    print("live console → %s   (Ctrl+C to stop)" % url)
    if not newest_log(arg):
        print("  note: no shintai_log_*.csv capture yet — run `shintai` to start one; the console fills in live.")
    subprocess.run(["open", url], check=False)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
        srv.shutdown()


if __name__ == "__main__":
    arg = next((a for a in sys.argv[1:] if not a.startswith("-")), None)
    serve(arg)
