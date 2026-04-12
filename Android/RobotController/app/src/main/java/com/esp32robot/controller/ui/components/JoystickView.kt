package com.esp32robot.controller.ui.components

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.min
import kotlin.math.sqrt

/**
 * JoystickView
 *
 * 自定义摇杆 View，直接继承 View（原生触摸处理，零额外延迟）
 * 触摸采样率等于屏幕硬件采样率（60~240Hz 均完整捕获）
 *
 * 回调 [onMove]：归一化坐标 normX, normY ∈ [-1, 1]，向右/向上为正
 * 松开时以 [onRelease] 回调，坐标均为 0
 */
class JoystickView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    /** 摇杆移动回调，normX/normY 范围 [-1, 1] */
    var onMove: ((normX: Float, normY: Float) -> Unit)? = null

    /** 摇杆松开回调 */
    var onRelease: (() -> Unit)? = null

    // ── 绘制属性 ─────────────────────────────────────────────────
    private val baseCirclePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x33FFFFFF
        style = Paint.Style.FILL
    }
    private val baseRingPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0x88FFFFFF.toInt()
        style = Paint.Style.STROKE
        strokeWidth = 3f
    }
    private val thumbPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF4CAF50.toInt()
        style = Paint.Style.FILL
    }
    private val thumbRingPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF81C784.toInt()
        style = Paint.Style.STROKE
        strokeWidth = 4f
    }

    // ── 几何参数 ─────────────────────────────────────────────────
    private var cx = 0f      // 中心 X
    private var cy = 0f      // 中心 Y
    private var maxRadius = 0f    // 摇杆最大移动半径
    private var thumbRadius = 0f  // 摇杆圆球半径

    // ── 当前摇杆位置 ─────────────────────────────────────────────
    private var thumbX = 0f
    private var thumbY = 0f
    private var isDragging = false

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        cx = w / 2f
        cy = h / 2f
        maxRadius = min(w, h) / 2f * 0.55f
        thumbRadius = min(w, h) / 2f * 0.25f
        thumbX = cx
        thumbY = cy
    }

    override fun onDraw(canvas: Canvas) {
        // 底盘外圈
        canvas.drawCircle(cx, cy, maxRadius * 1.2f, baseCirclePaint)
        canvas.drawCircle(cx, cy, maxRadius, baseRingPaint)

        // 十字准线
        baseRingPaint.alpha = 80
        canvas.drawLine(cx - maxRadius, cy, cx + maxRadius, cy, baseRingPaint)
        canvas.drawLine(cx, cy - maxRadius, cx, cy + maxRadius, baseRingPaint)
        baseRingPaint.alpha = 136

        // 摇杆球
        canvas.drawCircle(thumbX, thumbY, thumbRadius, thumbPaint)
        canvas.drawCircle(thumbX, thumbY, thumbRadius, thumbRingPaint)
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                isDragging = true
                updateThumb(event.x, event.y)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (isDragging) updateThumb(event.x, event.y)
                return true
            }
            MotionEvent.ACTION_UP,
            MotionEvent.ACTION_CANCEL,
            MotionEvent.ACTION_POINTER_UP -> {
                isDragging = false
                thumbX = cx
                thumbY = cy
                invalidate()
                onRelease?.invoke()
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private fun updateThumb(touchX: Float, touchY: Float) {
        val dx = touchX - cx
        val dy = touchY - cy
        val dist = sqrt(dx * dx + dy * dy)

        if (dist <= maxRadius) {
            thumbX = touchX
            thumbY = touchY
        } else {
            // 超出范围则限制在边缘
            val scale = maxRadius / dist
            thumbX = cx + dx * scale
            thumbY = cy + dy * scale
        }
        invalidate()

        // 归一化（Y 轴向上为正，与 MotionEvent Y 轴方向相反）
        val normX = (thumbX - cx) / maxRadius
        val normY = -(thumbY - cy) / maxRadius
        onMove?.invoke(normX, normY)
    }
}
