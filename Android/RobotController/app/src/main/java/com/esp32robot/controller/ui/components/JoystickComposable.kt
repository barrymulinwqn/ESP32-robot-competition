package com.esp32robot.controller.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import kotlin.math.min
import kotlin.math.sqrt

/**
 * Joystick（纯 Compose 实现）
 *
 * 使用 Canvas 绘制 + pointerInput 多点触控，彻底解决双摇杆同时响应问题。
 *
 * 问题根因：
 *   原 AndroidView(JoystickView) 依赖 Android View 触摸分发机制——
 *   第一根手指"锁定"了父 ViewGroup 的触摸焦点，第二个摇杆 View 完全
 *   收不到 ACTION_POINTER_DOWN 事件，导致双摇杆无法同时响应。
 *
 * 修复原理：
 *   纯 Compose pointerInput 对每个 composable 独立分发触摸事件，
 *   每个摇杆各自追踪一个 pointerId，互不干扰，支持真正的同时操作。
 *
 * 回调 [onMove]：归一化坐标 normX, normY ∈ [-1, 1]，向右/向上为正
 * 松开时以 [onRelease] 回调
 */
@Composable
fun Joystick(
    modifier: Modifier = Modifier,
    onMove: (normX: Float, normY: Float) -> Unit = { _, _ -> },
    onRelease: () -> Unit = {}
) {
    // rememberUpdatedState：让 pointerInput(Unit) 协程始终持有最新的回调引用，
    // 同时避免因回调 lambda 变化（如 isConnected 切换）而重启协程中断进行中的手势
    val latestOnMove by rememberUpdatedState(onMove)
    val latestOnRelease by rememberUpdatedState(onRelease)

    // 当前正在追踪的 pointer ID（null = 摇杆未激活）
    var trackingId by remember { mutableStateOf<Long?>(null) }
    // 摇杆球的当前位置（null = 居中 / 未拖动）
    var thumbPos by remember { mutableStateOf<Offset?>(null) }

    Canvas(
        modifier = modifier
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent(PointerEventPass.Main)
                        val cx   = size.width  / 2f
                        val cy   = size.height / 2f
                        val maxR = min(size.width, size.height) / 2f * 0.55f

                        for (change in event.changes) {
                            val id = change.id.value
                            when {
                                // ── 手指按下：空闲时接管此 pointer ──────────────
                                change.pressed && !change.previousPressed -> {
                                    if (trackingId == null) {
                                        trackingId = id
                                        val pos = clampToRadius(change.position, cx, cy, maxR)
                                        thumbPos = pos
                                        latestOnMove(
                                            (pos.x - cx) / maxR,
                                            -(pos.y - cy) / maxR
                                        )
                                        change.consume()
                                    }
                                }
                                // ── 手指移动：仅处理已追踪的 pointer ────────────
                                change.pressed && change.previousPressed && id == trackingId -> {
                                    val pos = clampToRadius(change.position, cx, cy, maxR)
                                    thumbPos = pos
                                    latestOnMove(
                                        (pos.x - cx) / maxR,
                                        -(pos.y - cy) / maxR
                                    )
                                    change.consume()
                                }
                                // ── 手指抬起 / 取消：归零并释放 pointer ──────────
                                !change.pressed && change.previousPressed && id == trackingId -> {
                                    trackingId = null
                                    thumbPos = null
                                    latestOnRelease()
                                    change.consume()
                                }
                            }
                        }
                    }
                }
            }
    ) {
        val cx     = size.width  / 2f
        val cy     = size.height / 2f
        val maxR   = min(size.width, size.height) / 2f * 0.55f
        val thumbR = min(size.width, size.height) / 2f * 0.25f
        val center = Offset(cx, cy)
        val thumb  = thumbPos ?: center

        // 底盘圆填充（~20% 透明白）
        drawCircle(color = Color(0x33FFFFFF), radius = maxR * 1.2f, center = center)
        // 底盘外圈（~53% 透明白）
        drawCircle(
            color  = Color(0x88FFFFFF.toInt()),
            radius = maxR,
            center = center,
            style  = Stroke(width = 3.dp.toPx())
        )
        // 十字准线（~31% 透明白）
        val crossColor = Color(0x50FFFFFF)
        drawLine(crossColor, Offset(cx - maxR, cy), Offset(cx + maxR, cy), 2.dp.toPx())
        drawLine(crossColor, Offset(cx, cy - maxR), Offset(cx, cy + maxR), 2.dp.toPx())
        // 摇杆球（绿色填充 + 浅绿描边）
        drawCircle(color = Color(0xFF4CAF50.toInt()), radius = thumbR, center = thumb)
        drawCircle(
            color  = Color(0xFF81C784.toInt()),
            radius = thumbR,
            center = thumb,
            style  = Stroke(width = 4.dp.toPx())
        )
    }
}

/**
 * 将触点位置限制在以 (cx, cy) 为圆心、maxR 为半径的圆内
 */
private fun clampToRadius(pos: Offset, cx: Float, cy: Float, maxR: Float): Offset {
    val dx   = pos.x - cx
    val dy   = pos.y - cy
    val dist = sqrt(dx * dx + dy * dy)
    return if (dist <= maxR) pos
    else Offset(cx + dx * (maxR / dist), cy + dy * (maxR / dist))
}
