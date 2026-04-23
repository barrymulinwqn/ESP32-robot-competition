package com.esp32robot.controller.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.esp32robot.controller.data.RobotWebSocketClient
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicInteger

/**
 * RobotViewModel
 *
 * 负责：
 *  1. 封装 WebSocket 连接生命周期
 *  2. 50Hz 定时发送电机指令（避免摇杆每帧直接发送造成 ESP32 过载）
 *  3. 激光/制动/停止等单次指令直接透传
 */
class RobotViewModel(application: Application) : AndroidViewModel(application) {

    val wsClient = RobotWebSocketClient()

    // 对外暴露状态流
    val connectionState = wsClient.connectionState
    val irCount = wsClient.irCount

    // ── 摇杆值缓存（原子操作，线程安全，无锁） ───────────────────
    private val pendingMotorA = AtomicInteger(0)   // 左电机 -255~255
    private val pendingMotorB = AtomicInteger(0)   // 右电机 -255~255

    private var motorLoopJob: Job? = null

    // ── 激光状态 ─────────────────────────────────────────────────
    var laserOn = kotlinx.coroutines.flow.MutableStateFlow(false)
        private set

    // ────────────────────────────────────────────────────────────
    //  连接管理
    // ────────────────────────────────────────────────────────────

    fun connect(
        ssid: String = "Robot-ESP32-XXXXXX",
        password: String = "12345678"
    ) {
        wsClient.connect(getApplication(), ssid, password)
        startMotorLoop()
    }

    fun disconnect() {
        stopMotorLoop()
        wsClient.disconnect(getApplication())
    }

    // ────────────────────────────────────────────────────────────
    //  50Hz 电机指令循环
    //  摇杆移动只更新缓存值，此循环统一以 20ms 间隔发送
    //  避免高采样率屏幕（120Hz）直接驱动 WebSocket 导致 ESP32 队列溢出
    // ────────────────────────────────────────────────────────────

    private fun startMotorLoop() {
        if (motorLoopJob?.isActive == true) return
        motorLoopJob = viewModelScope.launch {
            while (isActive) {
                wsClient.sendMotor(pendingMotorA.get(), pendingMotorB.get())
                delay(20L)   // 50 Hz
            }
        }
    }

    private fun stopMotorLoop() {
        motorLoopJob?.cancel()
        motorLoopJob = null
    }

    // ────────────────────────────────────────────────────────────
    //  摇杆输入（UI 层调用，只写缓存，不直接发网络）
    // ────────────────────────────────────────────────────────────

    // ────────────────────────────────────────────────────────────
    //  单摇杆 Arcade Drive 差速混控
    //  维持原有 Y 轴符号约定（normY=+1 上推 → 负 PWM → 机器人前进）
    //
    //  左电机 (A) = −clamp(Y + X, −1, 1) × 255
    //  右电机 (B) = −clamp(Y − X, −1, 1) × 255
    //
    //  效果：
    //    推上(Y=+1, X=0)  → A=-255, B=-255  前进
    //    推下(Y=-1, X=0)  → A=+255, B=+255  后退
    //    推右(Y=0,  X=+1) → A=-255, B=+255  右转（原地）
    //    推左(Y=0,  X=-1) → A=+255, B=-255  左转（原地）
    //    右上对角          → 左快右慢        右前弧线
    //    左上对角          → 左慢右快        左前弧线
    // ────────────────────────────────────────────────────────────
    fun onJoystick(normX: Float, normY: Float) {
        val left  = -(normY + normX).coerceIn(-1f, 1f)
        val right = -(normY - normX).coerceIn(-1f, 1f)
        pendingMotorA.set((left  * 255f).toInt().coerceIn(-255, 255))
        pendingMotorB.set((right * 255f).toInt().coerceIn(-255, 255))
    }

    /** 单摇杆松开：双电机归零（惰行） */
    fun onJoystickRelease() {
        pendingMotorA.set(0)
        pendingMotorB.set(0)
    }

    /**
     * 左摇杆 Y 轴 → 左电机 (Motor A) — 保留供双摇杆模式使用
     * @param normY 归一化 Y 值，范围 -1f（下）~ +1f（上）
     */
    fun onLeftJoystick(normX: Float, normY: Float) {
        pendingMotorA.set((-normY * 255).toInt().coerceIn(-255, 255))
    }

    fun onRightJoystick(normX: Float, normY: Float) {
        pendingMotorB.set((-normY * 255).toInt().coerceIn(-255, 255))
    }

    /** 摇杆松开归零 */
    fun onLeftJoystickRelease() = pendingMotorA.set(0)
    fun onRightJoystickRelease() = pendingMotorB.set(0)

    // ────────────────────────────────────────────────────────────
    //  直接指令
    // ────────────────────────────────────────────────────────────

    /** 急停（短路制动）*/
    fun brake() {
        pendingMotorA.set(0)
        pendingMotorB.set(0)
        wsClient.sendBrake()
    }

    /** 惰行停止 */
    fun stop() {
        pendingMotorA.set(0)
        pendingMotorB.set(0)
        wsClient.sendStop()
    }

    /** 激光切换 */
    fun toggleLaser() {
        val next = !laserOn.value
        laserOn.value = next
        wsClient.sendLaser(next)
    }

    fun setLaser(on: Boolean) {
        laserOn.value = on
        wsClient.sendLaser(on)
    }

    /** 重置命中计数 */
    fun resetIrCount() = wsClient.sendIrReset()

    // ────────────────────────────────────────────────────────────
    //  快捷运动指令（对应 硬件接线图.md § 8 真值表）
    //  均写入缓存，由 50Hz 循环发出，保持节流效果
    // ────────────────────────────────────────────────────────────

    fun moveForward(speed: Int = 200) {
        pendingMotorA.set(speed); pendingMotorB.set(speed)
    }

    fun moveBackward(speed: Int = 200) {
        pendingMotorA.set(-speed); pendingMotorB.set(-speed)
    }

    fun spinLeft(speed: Int = 180) {
        pendingMotorA.set(-speed); pendingMotorB.set(speed)
    }

    fun spinRight(speed: Int = 180) {
        pendingMotorA.set(speed); pendingMotorB.set(-speed)
    }

    override fun onCleared() {
        stopMotorLoop()
        wsClient.sendBrake()
        wsClient.disconnect(getApplication())
    }
}
