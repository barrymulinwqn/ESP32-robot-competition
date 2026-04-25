package com.esp32robot.controller.ui.components

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
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
import com.esp32robot.controller.viewmodel.SpeedLevel
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
    speedLevel: SpeedLevel = SpeedLevel.MEDIUM,
    onZonePress: (DirectionZone) -> Unit = {},
    onZoneRelease: () -> Unit = {}
) {
    val latestOnPress   by rememberUpdatedState(onZonePress)
    val latestOnRelease by rememberUpdatedState(onZoneRelease)

    var pressedZone by remember { mutableStateOf<DirectionZone?>(null) }
    val textMeasurer = rememberTextMeasurer()

    // ── 配色（随速度级别动态切换，300ms 过渡动画）─────────────────
    //   LOW  : 绿色主题  — 背景深绿，边框亮绿，急停圆保持红色（高对比）
    //   MEDIUM: 蓝色主题  — 保持原有蓝色配色
    //   HIGH : 红色主题  — 背景深红，边框亮红，急停圆改为橙色（防止与背景混淆）
    val colorBg by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFF051209)   // 近黑，绿色底调
            SpeedLevel.MEDIUM -> Color(0xFF0D1117)   // 近黑，蓝色底调
            SpeedLevel.HIGH   -> Color(0xFF0E0607)   // 近黑，红色底调
        }, animationSpec = tween(300), label = "padBg"
    )
    val colorCardinalNorm by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFF163D1C)   // 深森林绿
            SpeedLevel.MEDIUM -> Color(0xFF1B3A5C)   // 深海军蓝
            SpeedLevel.HIGH   -> Color(0xFF3D1212)   // 深暗红
        }, animationSpec = tween(300), label = "padCardinalNorm"
    )
    val colorCardinalPrs by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFF2E7D32)   // 中绿按下
            SpeedLevel.MEDIUM -> Color(0xFF1565C0)   // 中蓝按下
            SpeedLevel.HIGH   -> Color(0xFF8B0000)   // 深红按下
        }, animationSpec = tween(300), label = "padCardinalPrs"
    )
    val colorDiagNorm by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFF0F2D14)   // 更深森林绿
            SpeedLevel.MEDIUM -> Color(0xFF142840)   // 更深海军蓝
            SpeedLevel.HIGH   -> Color(0xFF2D0C0C)   // 更深暗红
        }, animationSpec = tween(300), label = "padDiagNorm"
    )
    val colorDiagPrs by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFF1B5E20)   // 深绿按下
            SpeedLevel.MEDIUM -> Color(0xFF0D47A1)   // 深蓝按下
            SpeedLevel.HIGH   -> Color(0xFF680000)   // 极深红按下
        }, animationSpec = tween(300), label = "padDiagPrs"
    )
    // 急停圆：高速时改用橙色，避免与深红背景对比度不足
    val colorCenterNorm by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.HIGH -> Color(0xFFFF6D00)     // 橙色（高对比）
            else            -> Color(0xFFE53935)     // 红色
        }, animationSpec = tween(300), label = "padCenterNorm"
    )
    val colorCenterPrs by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.HIGH -> Color(0xFFE65100)     // 深橙按下
            else            -> Color(0xFFB71C1C)     // 深红按下
        }, animationSpec = tween(300), label = "padCenterPrs"
    )
    val colorBorderOuter by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFF4CAF50)   // 亮绿边框
            SpeedLevel.MEDIUM -> Color(0xFF2196F3)   // 亮蓝边框
            SpeedLevel.HIGH   -> Color(0xFFEF5350)   // 亮红边框
        }, animationSpec = tween(300), label = "padBorderOuter"
    )
    val colorBorderInner by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFF2E7D32)   // 中绿内边框
            SpeedLevel.MEDIUM -> Color(0xFF1565C0)   // 中蓝内边框
            SpeedLevel.HIGH   -> Color(0xFFB71C1C)   // 深红内边框
        }, animationSpec = tween(300), label = "padBorderInner"
    )
    // 文字随主题微调，保持高可读性
    val colorTextPrimary by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFFE8F5E9)   // 近白，绿色微调
            SpeedLevel.MEDIUM -> Color(0xFFE0E0E0)   // 近白，中性
            SpeedLevel.HIGH   -> Color(0xFFFFEBEE)   // 近白，红色微调
        }, animationSpec = tween(300), label = "padTextPrimary"
    )
    val colorTextSub by animateColorAsState(
        targetValue = when (speedLevel) {
            SpeedLevel.LOW    -> Color(0xFFA5D6A7)   // 浅绿标签
            SpeedLevel.MEDIUM -> Color(0xFFB0BEC5)   // 浅灰蓝标签
            SpeedLevel.HIGH   -> Color(0xFFEF9A9A)   // 浅红标签
        }, animationSpec = tween(300), label = "padTextSub"
    )
    val colorTextCenter = Color(0xFFFFFFFF)           // 急停文字始终白色

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
                    fontSize   = 28.sp,
                    fontWeight = FontWeight.ExtraBold,
                    color      = colorTextPrimary
                )
            )
            // 功能标签（小字）
            val subLayout = textMeasurer.measure(
                text  = cfg.label,
                style = TextStyle(
                    fontSize   = 14.sp,
                    fontWeight = FontWeight.SemiBold,
                    color      = colorTextSub
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
                fontSize   = 18.sp,
                fontWeight = FontWeight.ExtraBold,
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
