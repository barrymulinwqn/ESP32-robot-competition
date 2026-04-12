package com.esp32robot.controller.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.FlashOn
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.LinkOff
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.esp32robot.controller.data.RobotWebSocketClient.ConnectionState
import com.esp32robot.controller.ui.components.Joystick
import com.esp32robot.controller.viewmodel.RobotViewModel

/**
 * ControlScreen
 *
 * 横屏主控制界面（screenOrientation=landscape）
 *
 * 布局（横屏）：
 *   ┌─────────┬─────────────────────┬─────────┐
 *   │  左摇杆  │    中央状态面板      │  右摇杆  │
 *   │ (Motor A)│  连接/激光/急停/IR  │ (Motor B)│
 *   └─────────┴─────────────────────┴─────────┘
 */
@Composable
fun ControlScreen(viewModel: RobotViewModel) {
    val connState by viewModel.connectionState.collectAsState()
    val irCount   by viewModel.irCount.collectAsState()
    val laserOn   by viewModel.laserOn.collectAsState()

    val isConnected = connState is ConnectionState.Connected

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            verticalAlignment = Alignment.CenterVertically
        ) {

            // ── 左摇杆（Motor A = 左电机）────────────────────────
            Joystick(
                modifier = Modifier
                    .size(220.dp)
                    .padding(4.dp),
                onMove = { normX, normY ->
                    if (isConnected) viewModel.onLeftJoystick(normX, normY)
                },
                onRelease = {
                    viewModel.onLeftJoystickRelease()
                }
            )

            Spacer(Modifier.width(8.dp))

            // ── 中央控制面板 ─────────────────────────────────────
            Column(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .padding(horizontal = 8.dp),
                verticalArrangement = Arrangement.SpaceEvenly,
                horizontalAlignment = Alignment.CenterHorizontally
            ) {

                // 状态标题
                Text(
                    text = "ESP32 机器人控制器",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onBackground
                )

                // 连接状态卡片
                ConnectionCard(connState = connState)

                // 连接/断开按钮
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = {
                            if (isConnected) viewModel.disconnect()
                            else viewModel.connect()
                        },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (isConnected)
                                MaterialTheme.colorScheme.error
                            else
                                MaterialTheme.colorScheme.primary
                        ),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Icon(
                            imageVector = if (isConnected) Icons.Filled.LinkOff else Icons.Filled.Link,
                            contentDescription = null
                        )
                        Spacer(Modifier.width(4.dp))
                        Text(if (isConnected) "断开" else "连接")
                    }
                }

                // 功能按钮组
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    // 急停
                    FilledIconButton(
                        onClick = { viewModel.brake() },
                        modifier = Modifier.size(56.dp),
                        colors = IconButtonDefaults.filledIconButtonColors(
                            containerColor = MaterialTheme.colorScheme.error
                        )
                    ) {
                        Icon(Icons.Filled.Stop, contentDescription = "急停", modifier = Modifier.size(28.dp))
                    }

                    // 激光开关
                    FilledIconButton(
                        onClick = { viewModel.toggleLaser() },
                        modifier = Modifier.size(56.dp),
                        colors = IconButtonDefaults.filledIconButtonColors(
                            containerColor = if (laserOn)
                                Color(0xFFFF9800)
                            else
                                MaterialTheme.colorScheme.surface
                        )
                    ) {
                        Icon(
                            Icons.Filled.FlashOn,
                            contentDescription = "激光",
                            modifier = Modifier.size(28.dp),
                            tint = if (laserOn) Color.White else MaterialTheme.colorScheme.onSurface
                        )
                    }

                    // 重置 IR 计数
                    FilledIconButton(
                        onClick = { viewModel.resetIrCount() },
                        modifier = Modifier.size(56.dp),
                        colors = IconButtonDefaults.filledIconButtonColors(
                            containerColor = MaterialTheme.colorScheme.secondary
                        )
                    ) {
                        Icon(Icons.Filled.Refresh, contentDescription = "重置计数", modifier = Modifier.size(28.dp))
                    }
                }

                // IR 命中计数显示
                IrCountCard(irCount = irCount)
            }

            Spacer(Modifier.width(8.dp))

            // ── 右摇杆（Motor B = 右电机）────────────────────────
            Joystick(
                modifier = Modifier
                    .size(220.dp)
                    .padding(4.dp),
                onMove = { normX, normY ->
                    if (isConnected) viewModel.onRightJoystick(normX, normY)
                },
                onRelease = {
                    viewModel.onRightJoystickRelease()
                }
            )
        }
    }
}

// ── 连接状态卡片 ─────────────────────────────────────────────────

@Composable
private fun ConnectionCard(connState: ConnectionState) {
    val (text, color) = when (connState) {
        is ConnectionState.Connected    -> "已连接  ${connState.ip}" to Color(0xFF4CAF50)
        is ConnectionState.Connecting   -> "正在连接..." to Color(0xFFFFC107)
        is ConnectionState.Error        -> "错误: ${connState.message}" to Color(0xFFF44336)
        is ConnectionState.Disconnected -> "未连接" to Color(0xFF9E9E9E)
    }

    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(10.dp)
                    .clip(CircleShape)
                    .background(color)
            )
            Spacer(Modifier.width(10.dp))
            Text(
                text = text,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface
            )
        }
    }
}

// ── IR 命中计数卡片 ──────────────────────────────────────────────

@Composable
private fun IrCountCard(irCount: Int) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = "激光命中次数",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
            )
            Spacer(Modifier.height(4.dp))
            Text(
                text = irCount.toString(),
                fontSize = 40.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                color = Color(0xFF4CAF50)
            )
        }
    }
}
