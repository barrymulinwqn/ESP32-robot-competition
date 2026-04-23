package com.esp32robot.controller.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
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
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.esp32robot.controller.data.RobotWebSocketClient.ConnectionState
import com.esp32robot.controller.ui.components.Joystick
import com.esp32robot.controller.viewmodel.RobotViewModel

/**
 * ControlScreen — 单摇杆差速控制界面（横屏）
 *
 * 布局（横屏）：
 *   ┌──────────────────────────────┬──────────────────┐
 *   │  ▲ 前进                      │  [状态] 已连接    │
 *   │◄ 左转  【大摇杆】  右转 ►    │  [连接/断开]      │
 *   │  ▼ 后退                      │  [⚡ 激光开关]    │
 *   │                              │  命中次数: 45 [↺] │
 *   │                              │  [■ 急 停]        │
 *   └──────────────────────────────┴──────────────────┘
 *
 * 控制逻辑（Arcade Drive 单摇杆差速混控）：
 *   左电机 (A) = −clamp(Y + X, −1, 1) × 255
 *   右电机 (B) = −clamp(Y − X, −1, 1) × 255
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
            .padding(horizontal = 12.dp, vertical = 8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {

            // ── 左侧：大摇杆区域（权重 1.7，约占 63%）──────────────
            Box(
                modifier = Modifier
                    .weight(1.7f)
                    .fillMaxHeight(),
                contentAlignment = Alignment.Center
            ) {
                Column(
                    modifier = Modifier.fillMaxSize(),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    // 方向提示：前进
                    Text(
                        text = "▲  前进",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.45f)
                    )
                    Spacer(Modifier.height(2.dp))

                    Row(
                        modifier = Modifier.weight(1f),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        // 方向提示：左转
                        Text(
                            text = "◄ 左转",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.45f)
                        )
                        Spacer(Modifier.width(4.dp))

                        // 摇杆：fillMaxHeight + aspectRatio(1f) 使其尽可能大且保持正圆
                        Joystick(
                            modifier = Modifier
                                .fillMaxHeight()
                                .aspectRatio(1f),
                            onMove = { normX, normY ->
                                if (isConnected) viewModel.onJoystick(normX, normY)
                            },
                            onRelease = {
                                viewModel.onJoystickRelease()
                            }
                        )

                        Spacer(Modifier.width(4.dp))
                        // 方向提示：右转
                        Text(
                            text = "右转 ►",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.45f)
                        )
                    }

                    Spacer(Modifier.height(2.dp))
                    // 方向提示：后退
                    Text(
                        text = "▼  后退",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.45f)
                    )
                }
            }

            // ── 右侧：控制面板（权重 1.0，约占 37%）──────────────────
            Column(
                modifier = Modifier
                    .weight(1.0f)
                    .fillMaxHeight()
                    .padding(vertical = 4.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {

                // 连接状态卡片
                ConnectionCard(
                    connState = connState,
                    modifier = Modifier.fillMaxWidth()
                )

                // 连接 / 断开按钮
                Button(
                    onClick = {
                        if (isConnected) viewModel.disconnect()
                        else viewModel.connect()
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(44.dp),
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
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = if (isConnected) "断开连接" else "连接热点",
                        fontWeight = FontWeight.Medium
                    )
                }

                // 激光开关按钮
                Button(
                    onClick = { viewModel.toggleLaser() },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(44.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (laserOn)
                            Color(0xFFFF9800)
                        else
                            MaterialTheme.colorScheme.secondaryContainer
                    ),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Icon(
                        Icons.Filled.FlashOn,
                        contentDescription = "激光",
                        modifier = Modifier.size(18.dp),
                        tint = if (laserOn)
                            Color.White
                        else
                            MaterialTheme.colorScheme.onSecondaryContainer
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = if (laserOn) "激光  ON" else "激光 OFF",
                        fontWeight = FontWeight.Medium,
                        color = if (laserOn)
                            Color.White
                        else
                            MaterialTheme.colorScheme.onSecondaryContainer
                    )
                }

                // IR 命中计数卡片（含重置按钮）
                IrCountCard(
                    irCount = irCount,
                    onReset = { viewModel.resetIrCount() },
                    modifier = Modifier.fillMaxWidth()
                )

                // 弹性填充，把急停按钮推到底部
                Spacer(Modifier.weight(1f))

                // 急停按钮（大红，置底，最高优先级操控）
                Button(
                    onClick = { viewModel.brake() },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(58.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.error
                    ),
                    shape = RoundedCornerShape(12.dp)
                ) {
                    Icon(
                        Icons.Filled.Stop,
                        contentDescription = "急停",
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(Modifier.width(8.dp))
                    Text(
                        text = "急  停",
                        fontWeight = FontWeight.Bold,
                        fontSize = 18.sp
                    )
                }
            }
        }
    }
}

// ── 连接状态卡片 ─────────────────────────────────────────────────

@Composable
private fun ConnectionCard(
    connState: ConnectionState,
    modifier: Modifier = Modifier
) {
    val (text, color) = when (connState) {
        is ConnectionState.Connected    -> "已连接  ${connState.ip}" to Color(0xFF4CAF50)
        is ConnectionState.Connecting   -> "正在连接..." to Color(0xFFFFC107)
        is ConnectionState.Error        -> "错误: ${connState.message}" to Color(0xFFF44336)
        is ConnectionState.Disconnected -> "未连接" to Color(0xFF9E9E9E)
    }

    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(12.dp),
        modifier = modifier
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(10.dp)
                    .clip(CircleShape)
                    .background(color)
            )
            Spacer(Modifier.width(8.dp))
            Text(
                text = text,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

// ── IR 命中计数卡片（内嵌重置按钮）────────────────────────────────

@Composable
private fun IrCountCard(
    irCount: Int,
    onReset: () -> Unit,
    modifier: Modifier = Modifier
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(12.dp),
        modifier = modifier
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "激光命中次数",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
                )
                IconButton(
                    onClick = onReset,
                    modifier = Modifier.size(28.dp)
                ) {
                    Icon(
                        Icons.Filled.Refresh,
                        contentDescription = "重置计数",
                        modifier = Modifier.size(18.dp),
                        tint = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f)
                    )
                }
            }
            Text(
                text = irCount.toString(),
                fontSize = 38.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                color = Color(0xFF4CAF50)
            )
        }
    }
}
