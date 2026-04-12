package com.esp32robot.controller.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// 深色主题（竞赛场景光线复杂，深色更易读）
private val RobotColorScheme = darkColorScheme(
    primary          = Color(0xFF4CAF50),   // 绿色：连接/激光激活
    onPrimary        = Color(0xFF000000),
    secondary        = Color(0xFF2196F3),   // 蓝色：摇杆高亮
    onSecondary      = Color(0xFFFFFFFF),
    error            = Color(0xFFF44336),   // 红色：断线/急停
    background       = Color(0xFF121212),
    surface          = Color(0xFF1E1E1E),
    onBackground     = Color(0xFFE0E0E0),
    onSurface        = Color(0xFFE0E0E0),
)

@Composable
fun RobotControllerTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = RobotColorScheme,
        content = content
    )
}
