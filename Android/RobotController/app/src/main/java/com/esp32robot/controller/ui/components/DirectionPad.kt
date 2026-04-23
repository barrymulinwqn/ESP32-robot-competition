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
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.sp
import com.esp32robot.controller.viewmodel.DirectionZone
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * DirectionPad — 九宫格大圆形方向按钮
 *
 * 外观：一个大圆，分为 8 个扇形区域 + 1 个中心红色急停圆。
 * 交互：按下某区域即持续发送对应方向指令，抬起手指立即停止。
 *
 * 角度约定（Android Canvas 坐标系，y 轴向下）：
 *   0°   = 右 (East)
 *   90°  = 下 (South)
 *   180° = 左 (West)
 *   270° = 上 (North)
 *
 * 区域划分（每区 45°，均分）：
 *   UP        : 247.5° ~ 292.5°   ↑ 前进
 *   UP_RIGHT  : 292.5° ~ 337.5°   ↗ 右前转弯
 *   RIGHT     : 337.5° ~ 22.5°    → 原地右旋
 *   DOWN_RIGHT:  22.5° ~  67.5°   ↘ 右后转弯
 *   DOWN      :  67.5° ~ 112.5°   ↓ 后退
 *   DOWN_LEFT : 112.5° ~ 157.5°   ↙ 左后转弯
 *   LEFT      : 157.5° ~ 202.5°   ← 原地左旋
 *   UP_LEFT   : 202.5° ~ 247.5°   ↖ 左前转弯
 *   CENTER    : 中心圆（r < 30%）  ● 急停
 */

// ── 区域配置 ─────────────────────────────────────────────────────

private data class ZoneConfig(
    val zone: DirectionZone,
    /** 扇形中心角（度，Canvas 坐标系：0°=右，顺时针） */
    val centerAngleDeg: Float,
    /** 显示在扇形中心的箭头符号 */
    val arrow: String,
    /** 显示在箭头下方的功能标签 */
    val label: String
)

private val ZONE_CONFIGS = listOf(
    ZoneConfig(DirectionZone.UP,         270f, "↑", "前进"),
    ZoneConfig(DirectionZone.UP_RIGHT,   315f, "↗", "右前"),
    ZoneConfig(DirectionZone.RIGHT,        0f, "→", "右旋"),
    ZoneConfig(DirectionZone.DOWN_RIGHT,  45f, "↘", "右后"),
    ZoneConfig(DirectionZone.DOWN,        90f, "↓", "后退"),
    ZoneConfig(DirectionZone.DOWN_LEFT,  135f, "↙", "左后"),
    ZoneConfig(DirectionZone.LEFT,       180f, "←", "左旋"),
    ZoneConfig(DirectionZone.UP_LEFT,    225f, "↖", "左前"),
)

// 角度范围 → DirectionZone（处理 0° 跨越问题）
private fun angleToZone(angleDeg: Float): DirectionZone {
    val a = ((angleDeg % 360f) + 360f) % 360f
    return when {
        a < 22.5f || a >= 337.5f -> DirectionZone.RIGHT
        a < 67.5f                -> DirectionZone.DOWN_RIGHT
        a < 112.5f               -> DirectionZone.DOWN
        a < 157.5f               -> DirectionZone.DOWN_LEFT
        a < 202.5f               -> DirectionZone.LEFT
        a < 247.5f               -> DirectionZone.UP_LEFT
        a < 292.5f               -> DirectionZone.UP
        else                     -> DirectionZone.UP_RIGHT
    }
}

// ── Composable ────────────────────────────────────────────────────

@Composable
fun DirectionPad(
    modifier: Modifier = Modifier,
    onZonePress: (DirectionZone) -> Unit = {},
    onZoneRelease: () -> Unit = {}
) {
    val latestOnPress   by rememberUpdatedState(onZonePress)
    val latestOnRelease by rememberUpdatedState(onZoneRelease)

    var pressedZone by remember { mutableStateOf<DirectionZone?>(null) }
    val textMeasurer = rememberTextMeasurer()

    // ── 配色 ────────────────────────────────────────────────────
    val colorBg           = Color(0xFF0D1117)  // 深黑蓝背景
    val colorCardinalNorm = Color(0xFF1B3A5C)  // 主方向（前/后/左旋/右旋）正常色
    val colorCardinalPrs  = Color(0xFF1565C0)  // 主方向按下高亮
    val colorDiagNorm     = Color(0xFF142840)  // 斜向（转弯）正常色
    val colorDiagPrs      = Color(0xFF0D47A1)  // 斜向按下高亮
    val colorCenterNorm   = Color(0xFFE53935)  // 急停圆正常（红）
    val colorCenterPrs    = Color(0xFFB71C1C)  // 急停圆按下（深红）
    val colorBorderOuter  = Color(0xFF2196F3)  // 外圆边框
    val colorBorderInner  = Color(0xFF1565C0)  // 内圆边框
    val colorTextPrimary  = Color(0xFFE0E0E0)  // 箭头文字
    val colorTextSub      = Color(0xFFB0BEC5)  // 功能标签文字
    val colorTextCenter   = Color(0xFFFFFFFF)  // 急停文字

    Canvas(
        modifier = modifier
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent(PointerEventPass.Main)
                        val cx     = size.width  / 2f
                        val cy     = size.height / 2f
                        val outerR = min(size.width, size.height) / 2f * 0.96f
                        val innerR = outerR * 0.30f

                        for (change in event.changes) {
                            val dx   = change.position.x - cx
                            val dy   = change.position.y - cy
                            val dist = sqrt(dx * dx + dy * dy)

                            when {
                                // 手指按下：判断区域并回调
                                change.pressed && !change.previousPressed -> {
                                    if (dist <= outerR) {
                                        val zone = if (dist <= innerR) {
                                            DirectionZone.CENTER
                                        } else {
                                            val deg = atan2(dy, dx) * (180f / Math.PI.toFloat())
                                            angleToZone(deg)
                                        }
                                        pressedZone = zone
                                        latestOnPress(zone)
                                        change.consume()
                                    }
                                }
                                // 手指抬起：停止指令
                                !change.pressed && change.previousPressed -> {
                                    pressedZone = null
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
        val outerR = min(size.width, size.height) / 2f * 0.96f
        val innerR = outerR * 0.30f

        // ── 背景大圆 ───────────────────────────────────────────
        drawCircle(color = colorBg, radius = outerR, center = Offset(cx, cy))

        // ── 8 个扇形（各 40°，相邻间留 5° 空隙）─────────────────
        val sweepDeg = 40f
        val gapDeg   = 5f
        for (cfg in ZONE_CONFIGS) {
            val startAngle = cfg.centerAngleDeg - sweepDeg / 2f + gapDeg / 2f
            val isPressed  = pressedZone == cfg.zone
            val isDiag = cfg.zone in setOf(
                DirectionZone.UP_LEFT, DirectionZone.UP_RIGHT,
                DirectionZone.DOWN_LEFT, DirectionZone.DOWN_RIGHT
            )
            val segColor = when {
                isPressed && isDiag -> colorDiagPrs
                isPressed           -> colorCardinalPrs
                isDiag              -> colorDiagNorm
                else                -> colorCardinalNorm
            }
            drawArc(
                color      = segColor,
                startAngle = startAngle,
                sweepAngle = sweepDeg - gapDeg,
                useCenter  = true,
                topLeft    = Offset(cx - outerR, cy - outerR),
                size       = Size(outerR * 2, outerR * 2)
            )
        }

        // ── 外圆描边 ────────────────────────────────────────────
        drawCircle(
            color  = colorBorderOuter.copy(alpha = 0.8f),
            radius = outerR,
            center = Offset(cx, cy),
            style  = Stroke(width = 3f)
        )

        // ── 内圆描边（区分中心按钮与扇形） ─────────────────────
        drawCircle(
            color  = colorBorderInner.copy(alpha = 0.5f),
            radius = innerR,
            center = Offset(cx, cy),
            style  = Stroke(width = 2f)
        )

        // ── 中心急停圆 ─────────────────────────────────────────
        val isCenterPressed = pressedZone == DirectionZone.CENTER
        drawCircle(
            color  = if (isCenterPressed) colorCenterPrs else colorCenterNorm,
            radius = innerR,
            center = Offset(cx, cy)
        )

        // ── 标签文字：箭头 + 功能名 ────────────────────────────
        val labelRadius = (innerR + outerR) * 0.54f   // 文字放在扇形中间

        for (cfg in ZONE_CONFIGS) {
            val rad = cfg.centerAngleDeg * (Math.PI / 180.0)
            val lx  = cx + cos(rad).toFloat() * labelRadius
            val ly  = cy + sin(rad).toFloat() * labelRadius

            // 箭头（稍大字号）
            val arrowLayout = textMeasurer.measure(
                text  = cfg.arrow,
                style = TextStyle(
                    fontSize   = 20.sp,
                    fontWeight = FontWeight.Bold,
                    color      = colorTextPrimary
                )
            )
            // 功能标签（小字）
            val subLayout = textMeasurer.measure(
                text  = cfg.label,
                style = TextStyle(
                    fontSize = 10.sp,
                    color    = colorTextSub
                )
            )

            val totalH  = arrowLayout.size.height + subLayout.size.height + 2
            val arrowX  = lx - arrowLayout.size.width  / 2f
            val arrowY  = ly - totalH / 2f
            val subX    = lx - subLayout.size.width / 2f
            val subY    = arrowY + arrowLayout.size.height + 2f

            drawText(arrowLayout, topLeft = Offset(arrowX, arrowY))
            drawText(subLayout,   topLeft = Offset(subX,   subY))
        }

        // ── 急停标签 ────────────────────────────────────────────
        val stopLayout = textMeasurer.measure(
            text  = "急停",
            style = TextStyle(
                fontSize   = 14.sp,
                fontWeight = FontWeight.Bold,
                color      = colorTextCenter
            )
        )
        drawText(
            stopLayout,
            topLeft = Offset(
                cx - stopLayout.size.width  / 2f,
                cy - stopLayout.size.height / 2f
            )
        )
    }
}
