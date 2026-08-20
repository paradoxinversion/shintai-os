package com.saboteur.shintaioperator.ui

import androidx.compose.ui.graphics.Color

/** DIAGNOSTIC TEMP — one contact on [AScope] (specs/zokyo/hitokage.md HK-4), not part of
 *  CONTRACT.md. [color] is resolved by the caller (proximity-band knowledge stays out of
 *  the UI-primitive AScope). Strip alongside the rest of "DIAGNOSTIC TEMP". */
data class ScopeBlip(val distanceMm: Int, val strengthRaw: Int, val color: Color)
