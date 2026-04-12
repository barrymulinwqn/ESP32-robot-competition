package com.esp32robot.controller.ui.components

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView

/**
 * JoystickComposable
 *
 * 将 JoystickView（原生 View）包装为 Compose 组件
 * 通过 AndroidView 互操作，保留原生触摸采样率优势
 */
@Composable
fun Joystick(
    modifier: Modifier = Modifier,
    onMove: (normX: Float, normY: Float) -> Unit = { _, _ -> },
    onRelease: () -> Unit = {}
) {
    AndroidView(
        modifier = modifier,
        factory = { context ->
            JoystickView(context).apply {
                this.onMove = onMove
                this.onRelease = onRelease
            }
        },
        update = { view ->
            view.onMove = onMove
            view.onRelease = onRelease
        }
    )
}
