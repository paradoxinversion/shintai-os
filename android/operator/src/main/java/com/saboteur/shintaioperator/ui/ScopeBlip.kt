package com.saboteur.shintaioperator.ui

import androidx.compose.ui.graphics.Color

/** One contact on [AScope] (specs/zokyo/hitokage.md). [color] is resolved by the caller
 *  (proximity-band knowledge stays out of the UI-primitive AScope). */
data class ScopeBlip(val distanceMm: Int, val strengthRaw: Int, val color: Color)
