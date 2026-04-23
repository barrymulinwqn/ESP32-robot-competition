package com.esp32robot.controller.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
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
import androidx.compose.material.icons.filled.FlashOff
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
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.RadioButton
import androidx.compose.material3.RadioButtonDefaults
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
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.esp32robot.controller.data.RobotWebSocketClient.ConnectionState
import com.esp32robot.controller.ui.components.DirectionPad
import com.esp32robot.controller.viewmodel.RobotViewModel
import com.esp32robot.controller.viewmodel.SpeedLevel
import kotlin.math.abs

/**
 * ControlScreen — 九宫格方向盘竞赛控制界面（横屏）
 *
 * 布局（横屏）：
 * ┌──────────────┬────────────────────────────────┬──────────────┐
 * │  [电机转速]  │              [速度: ○低 ●中 ○高]│  [连接状态]  │
 * │              │                                │  [连接/断开] │
 * │  左: ████░   │         ┌───────────┐          │  [激光开关]  │
 * │       80%    │         │ ↖  ↑  ↗   │          │  [命中次数]  │
 * │  前进        │         │ ←  ●  →   │          │             │
 * │              │         │ ↙  ↓  ↘   │          │  [■ 急  停] │
 * │  右: ██░░░   │         └───────────┘          │             │
 * └──────────────┴────────────────────────────────┴──────────────┘
 */
@Composable
fun ControlScreen(viewModel: RobotViewModel) {
    val connState  by viewModel.connectionState.collectAsState()
    val irCount    by viewModel.irCount.collectAsState()
    val laserOn    by viewModel.laserOn.collectAsState()
    val speedLevel by viewModel.speedLevel.collectAsState()
    val motorA     by viewModel.displayMotorA.collectAsState()
    val motorB     by viewModel.displayMotorB.collectAsState()

    val isConnected = connState is ConnectionState.Connected

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF0A0E1A))
            .padding(horizontal = 8.dp, vertical = 6.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // ── 左侧：电机转速面板（22%）────────────────────────────
            MotorSpeedPanel(
                motorA   = motorA,
                motorB   = motorB,
                modifier = Modifier.weight(0.22f).fillMaxHeight()
            )

            // ── 中央：方向盘 + 速度选择器（50%）─────────────────────
            Box(modifier = Modifier.weight(0.50f).fillMaxHeight()) {
                // 速度选择器：悬浮于方向盘右上角
                SpeedSelector(
                    selected = speedLevel,
                    onSelect = { viewModel.setSpeedLevel(it) },
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(top = 2.dp, end = 2.dp)
                )
                // 九宫格大圆方向盘：居中
                DirectionPad(
                    modifier = Modifier
                        .fillMaxHeight()
                        .aspectRatio(1f)
                        .align(Alignment.Center),
                    onZonePress   = { zone -> if (isConnected) viewModel.sendDirection(zone) },
                    onZoneRelease = { viewModel.releaseDirection() }
                )
            }

            // ── 右侧：连接与功能控制（28%）──────────────────────────
            Column(
                modifier = Modifier
                    .weight(0.28f)
                    .fillMaxHeight()
                    .padding(vertical = 2.dp),
                verticalArrangement = Arrangement.spacedBy(7.dp)
            ) {
                ConnectionCard(connState = connState, modifier = Modifier.fillMaxWidth())

                // 连接 / 断开
                Button(
                    onClick = { if (isConnected) viewModel.disconnect() else viewModel.connect() },
                    modifier = Modifier.fillMaxWidth().height(46.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (isConnected) Color(0xFFB71C1C) else Color(0xFF1565C0)
                    ),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(
                        imageVector        = if (isConnected) Icons.Filled.LinkOff else Icons.Filled.Link,
                        contentDescription = null,
                        modifier           = Modifier.size(18.dp)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text       = if (isConnected) "断开连接" else "连接热点",
                        fontWeight = FontWeight.SemiBold,
                        fontSize   = 14.sp
                    )
                }

                // 激光开关（颜色动画）
                val laserBg by animateColorAsState(
                    targetValue   = if (laserOn) Color(0xFFE65100) else Color(0xFF1A2332),
                    animationSpec = tween(200),
                    label         = "laserBg"
                )
                Button(
                    onClick  = { viewModel.toggleLaser() },
                    modifier = Modifier.fillMaxWidth().height(46.dp),
                    colors   = ButtonDefaults.buttonColors(containerColor = laserBg),
                    shape    = RoundedCornerShape(10.dp)
                ) {
                    Icon(
                        imageVector        = if (laserOn) Icons.Filled.FlashOn else Icons.Filled.FlashOff,
                        contentDescription = "激光",
                        modifier           = Modifier.size(18.dp),
                        tint               = Color.White
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text       = if (laserOn) "激光  ON" else "激光 OFF",
                        fontWeight = FontWeight.SemiBold,
                        fontSize   = 14.sp,
                        color      = Color.White
                    )
                }

                IrCountCard(
                    irCount  = irCount,
                    onReset  = { viewModel.resetIrCount() },
                    modifier = Modifier.fillMaxWidth()
                )

                Spacer(Modifier.weight(1f))

                // 急停（大红，置底）
                Button(
                    onClick  = { viewModel.brake() },
                    modifier = Modifier.fillMaxWidth().height(62.dp),
                    colors   = ButtonDefaults.buttonColors(containerColor = Color(0xFFD32F2F)),
                    shape    = RoundedCornerShape(10.dp)
                ) {
                    Icon(
                        Icons.Filled.Stop,
                        contentDescription = "急停",
                        modifier           = Modifier.size(26.dp),
                        tint               = Color.White
                    )
                    Spacer(Modifier.width(8.dp))
                    Text(
                        text       = "急  停",
                        fontWeight = FontWeight.ExtraBold,
                        fontSize   = 18.sp,
                        color      = Color.White
                    )
                }
            }
        }
    }
}

// ── 电机转速面板 ─────────────────────────────────────────────────────

@Composable
private fun MotorSpeedPanel(
    motorA: Int,
    motorB: Int,
    modifier: Modifier = Modifier
) {
    Card(
        colors   = CardDefaults.cardColors(containerColor = Color(0xFF111827)),
        shape    = RoundedCornerShape(12.dp),
        modifier = modifier
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 10.dp, vertical = 10.dp),
            verticalArrangement   = Arrangement.spacedBy(10.dp),
            horizontalAlignment   = Alignment.CenterHorizontally
        ) {
            Text(
                text          = "电 机 转 速",
                fontSize      = 11.sp,
                color         = Color(0xFF78909C),
                fontWeight    = FontWeight.SemiBold,
                letterSpacing = 1.5.sp
            )
            MotorSpeedBar(label = "左电机", value = motorA, modifier = Modifier.fillMaxWidth())
            MotorSpeedBar(label = "右电机", value = motorB, modifier = Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun MotorSpeedBar(
    label: String,
    value: Int,   // -255（反转=前进）~ 0 ~ +255（正转=后退）
    modifier: Modifier = Modifier
) {
    val fraction  = abs(value) / 255f
    val isForward = value < 0   // 负值 = 车体前进方向
    val isStopped = value == 0
    val barColor = when {
        isStopped -> Color(0xFF37474F)
        isForward -> Color(0xFF2E7D32)
        else      -> Color(0xFFBF360C)
    }
    val dirLabel = when {
        isStopped -> "停止"
        isForward -> "▲ 前进"
        else      -> "▼ 后退"
    }

    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(3.dp)) {
        Row(
            modifier              = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment     = Alignment.CenterVertically
        ) {
            Text(label,    fontSize = 11.sp, color = Color(0xFF78909C), fontWeight = FontWeight.Medium)
            Text(dirLabel, fontSize = 10.sp, color = barColor)
        }
        LinearProgressIndicator(
            progress   = { fraction },
            modifier   = Modifier
                .fillMaxWidth()
                .height(10.dp)
                .clip(RoundedCornerShape(5.dp)),
            color      = barColor,
            trackColor = Color(0xFF1E2A3A)
        )
        Text(
            text      = "${(fraction * 100).toInt()}%",
            fontSize  = 10.sp,
            color     = barColor,
            fontWeight = FontWeight.Bold,
            modifier  = Modifier.fillMaxWidth(),
            textAlign = TextAlign.End
        )
    }
}

// ── 速度级别选择器 ───────────────────────────────────────────────────

@Composable
private fun SpeedSelector(
    selected: SpeedLevel,
    onSelect: (SpeedLevel) -> Unit,
    modifier: Modifier = Modifier
) {
    Card(
        colors   = CardDefaults.cardColors(containerColor = Color(0xE6111827)),
        shape    = RoundedCornerShape(10.dp),
        modifier = modifier
    ) {
        Column(
            modifier            = Modifier.padding(horizontal = 8.dp, vertical = 6.dp),
            verticalArrangement = Arrangement.spacedBy(1.dp),
            horizontalAlignment = Alignment.Start
        ) {
            Text(
                text          = "速度级别",
                fontSize      = 10.sp,
                color         = Color(0xFF78909C),
                fontWeight    = FontWeight.SemiBold,
                letterSpacing = 0.5.sp
            )
            SpeedLevel.entries.forEach { level ->
                val isSelected = selected == level
                val levelColor = when (level) {
                    SpeedLevel.LOW    -> Color(0xFF43A047)
                    SpeedLevel.MEDIUM -> Color(0xFF1E88E5)
                    SpeedLevel.HIGH   -> Color(0xFFE53935)
                }
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier          = Modifier.height(30.dp)
                ) {
                    RadioButton(
                        selected = isSelected,
                        onClick  = { onSelect(level) },
                        modifier = Modifier.size(20.dp),
                        colors   = RadioButtonDefaults.colors(
                            selectedColor   = levelColor,
                            unselectedColor = Color(0xFF455A64)
                        )
                    )
                    Spacer(Modifier.width(4.dp))
                    Text(
                        text       = level.label,
                        fontSize   = 12.sp,
                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                        color      = if (isSelected) levelColor else Color(0xFF607D8B)
                    )
                    if (isSelected) {
                        Spacer(Modifier.width(4.dp))
                        Text(
                            text  = "(${level.pwm})",
                            fontSize = 9.sp,
                            color = levelColor.copy(alpha = 0.65f)
                        )
                    }
                }
            }
        }
    }
}

// ── 连接状态卡片 ─────────────────────────────────────────────────────

@Composable
private fun ConnectionCard(
    connState: ConnectionState,
    modifier: Modifier = Modifier
) {
    val (text, dotColor) = when (connState) {
        is ConnectionState.Connected    -> "已连接  ${connState.ip}" to Color(0xFF43A047)
        is ConnectionState.Connecting   -> "正在连接..." to Color(0xFFFFA000)
        is ConnectionState.Error        -> "错误: ${connState.message}" to Color(0xFFE53935)
        is ConnectionState.Disconnected -> "未连接" to Color(0xFF546E7A)
    }
    Card(
        colors   = CardDefaults.cardColors(containerColor = Color(0xFF111827)),
        shape    = RoundedCornerShape(10.dp),
        modifier = modifier
    ) {
        Row(
            modifier          = Modifier.padding(horizontal = 10.dp, vertical = 9.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(modifier = Modifier.size(9.dp).clip(CircleShape).background(dotColor))
            Spacer(Modifier.width(8.dp))
            Text(
                text     = text,
                fontSize = 11.sp,
                color    = Color(0xFFCFD8DC),
                maxLines = 2,
                overflow = TextOverflow.Ellipsis
            )
        }
    }
}

// ── IR 命中计数卡片 ──────────────────────────────────────────────────

@Composable
private fun IrCountCard(
    irCount: Int,
    onReset: () -> Unit,
    modifier: Modifier = Modifier
) {
    Card(
        colors   = CardDefaults.cardColors(containerColor = Color(0xFF111827)),
        shape    = RoundedCornerShape(10.dp),
        modifier = modifier
    ) {
        Column(
            modifier            = Modifier.padding(horizontal = 10.dp, vertical = 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(
                modifier              = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment     = Alignment.CenterVertically
            ) {
                Text(text = "激光命中次数", fontSize = 11.sp, color = Color(0xFF78909C))
                IconButton(onClick = onReset, modifier = Modifier.size(26.dp)) {
                    Icon(
                        Icons.Filled.Refresh,
                        contentDescription = "重置",
                        modifier           = Modifier.size(16.dp),
                        tint               = Color(0xFF455A64)
                    )
                }
            }
            Text(
                text       = irCount.toString(),
                fontSize   = 38.sp,
                fontWeight = FontWeight.ExtraBold,
                fontFamily = FontFamily.Monospace,
                color      = Color(0xFF66BB6A)
            )
        }
    }
}
