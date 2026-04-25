package com.esp32robot.controller.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.esp32robot.controller.data.RobotWebSocketClient
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicInteger

// ── 速度级别 ────────────────────────────────────────────────────────
enum class SpeedLevel(val pwm: Int, val label: String) {
    LOW(100, "低速"),
    MEDIUM(185, "中速"),
    HIGH(255, "高速")
}

// ── 方向区域（九宫格大按钮的九个区域）────────────────────────────────
enum class DirectionZone {
    UP,          // 前进
    UP_RIGHT,    // 右前转弯
    RIGHT,       // 原地右旋转
    DOWN_RIGHT,  // 右后转弯
    DOWN,        // 后退
    DOWN_LEFT,   // 左后转弯
    LEFT,        // 原地左旋转
    UP_LEFT,     // 左前转弯
    CENTER       // 急停
}

/**
 * RobotViewModel
 *
 * 负责：
 *  1. 封装 WebSocket 连接生命周期
 *  2. 50Hz 定时发送电机指令（方向按钮只更新缓存，循环统一发送）
 *  3. 激光/制动/停止等单次指令直接透传
 *  4. 速度级别管理（低速/中速/高速）
 *  5. 电机转速实时状态（供 UI 进度条显示）
 */
class RobotViewModel(application: Application) : AndroidViewModel(application) {

    val wsClient = RobotWebSocketClient()

    // 对外暴露状态流
    val connectionState = wsClient.connectionState
    val irCount = wsClient.irCount

    // ── 电机指令缓存（原子操作，线程安全，无锁） ──────────────────
    private val pendingMotorA = AtomicInteger(0)   // 左电机 -255~255
    private val pendingMotorB = AtomicInteger(0)   // 右电机 -255~255

    private var motorLoopJob: Job? = null

    // ── 速度级别 ─────────────────────────────────────────────────
    private val _speedLevel = MutableStateFlow(SpeedLevel.MEDIUM)
    val speedLevel: StateFlow<SpeedLevel> = _speedLevel.asStateFlow()

    // ── 电机实时转速（用于进度条显示，范围 -255~255）─────────────
    private val _displayMotorA = MutableStateFlow(0)
    val displayMotorA: StateFlow<Int> = _displayMotorA.asStateFlow()

    private val _displayMotorB = MutableStateFlow(0)
    val displayMotorB: StateFlow<Int> = _displayMotorB.asStateFlow()

    // ── 激光状态 ─────────────────────────────────────────────────
    private val _laserOn = MutableStateFlow(false)
    val laserOn: StateFlow<Boolean> = _laserOn.asStateFlow()

    // ────────────────────────────────────────────────────────────
    //  连接管理
    // ────────────────────────────────────────────────────────────

    fun connect(
        ssid: String = "Robot-ESP32-Barry",
        password: String = "rockwellrobot1234"
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
    //  方向按钮按下只更新缓存值，此循环以 20ms 间隔统一发送
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
    //  速度级别
    // ────────────────────────────────────────────────────────────

    fun setSpeedLevel(level: SpeedLevel) {
        _speedLevel.value = level
    }

    // ────────────────────────────────────────────────────────────
    //  九宫格方向按钮控制
    //
    //  方向 → 电机值映射（与摇杆 Arcade Drive 约定一致）：
    //    a 负 = 左电机反转 = 车体左侧前进
    //    b 负 = 右电机反转 = 车体右侧前进
    //    推上(前进): A=-spd, B=-spd
    //    推下(后退): A=+spd, B=+spd
    //    推左(原地左旋): A=+spd, B=-spd
    //    推右(原地右旋): A=-spd, B=+spd
    //    左前转弯: 左轮半速, 右轮全速 → A=-spd/2, B=-spd
    //    右前转弯: 左轮全速, 右轮半速 → A=-spd, B=-spd/2
    //    左后转弯: 左轮半速后退, 右轮全速后退 → A=+spd/2, B=+spd
    //    右后转弯: 左轮全速后退, 右轮半速后退 → A=+spd, B=+spd/2
    // ────────────────────────────────────────────────────────────

    fun sendDirection(zone: DirectionZone) {
        val spd = _speedLevel.value.pwm
        val half = spd / 2
        when (zone) {
            DirectionZone.UP         -> setMotors(-spd,   -spd)
            DirectionZone.DOWN       -> setMotors( spd,    spd)
            DirectionZone.LEFT       -> setMotors( spd,   -spd)
            DirectionZone.RIGHT      -> setMotors(-spd,    spd)
            DirectionZone.UP_LEFT    -> setMotors(-half,  -spd)
            DirectionZone.UP_RIGHT   -> setMotors(-spd,  -half)
            DirectionZone.DOWN_LEFT  -> setMotors( half,   spd)
            DirectionZone.DOWN_RIGHT -> setMotors( spd,   half)
            DirectionZone.CENTER     -> brake()
        }
    }

    /** 方向按钮松开：双电机归零（惰行） */
    fun releaseDirection() {
        setMotors(0, 0)
    }

    private fun setMotors(a: Int, b: Int) {
        pendingMotorA.set(a)
        pendingMotorB.set(b)
        _displayMotorA.value = a
        _displayMotorB.value = b
    }

    // ────────────────────────────────────────────────────────────
    //  摇杆输入（保留以兼容旧代码，实际已由方向按钮取代）
    // ────────────────────────────────────────────────────────────

    fun onJoystick(normX: Float, normY: Float) {
        val left  = -(normY + normX).coerceIn(-1f, 1f)
        val right = -(normY - normX).coerceIn(-1f, 1f)
        setMotors(
            (left  * 255f).toInt().coerceIn(-255, 255),
            (right * 255f).toInt().coerceIn(-255, 255)
        )
    }

    fun onJoystickRelease() = releaseDirection()

    fun onLeftJoystick(normX: Float, normY: Float) {
        pendingMotorA.set((-normY * 255).toInt().coerceIn(-255, 255))
    }

    fun onRightJoystick(normX: Float, normY: Float) {
        pendingMotorB.set((-normY * 255).toInt().coerceIn(-255, 255))
    }

    fun onLeftJoystickRelease()  = pendingMotorA.set(0)
    fun onRightJoystickRelease() = pendingMotorB.set(0)

    // ────────────────────────────────────────────────────────────
    //  直接指令
    // ────────────────────────────────────────────────────────────

    /** 急停（短路制动） */
    fun brake() {
        setMotors(0, 0)
        wsClient.sendBrake()
    }

    /** 惰行停止 */
    fun stop() {
        setMotors(0, 0)
        wsClient.sendStop()
    }

    /** 激光切换 */
    fun toggleLaser() {
        val next = !_laserOn.value
        _laserOn.value = next
        wsClient.sendLaser(next)
    }

    fun setLaser(on: Boolean) {
        _laserOn.value = on
        wsClient.sendLaser(on)
    }

    /** 重置命中计数 */
    fun resetIrCount() = wsClient.sendIrReset()

    // ────────────────────────────────────────────────────────────
    //  快捷运动指令（对应 硬件接线图.md § 8 真值表）
    // ────────────────────────────────────────────────────────────

    fun moveForward(speed: Int = 200)  = setMotors(-speed, -speed)
    fun moveBackward(speed: Int = 200) = setMotors( speed,  speed)
    fun spinLeft(speed: Int = 180)     = setMotors( speed, -speed)
    fun spinRight(speed: Int = 180)    = setMotors(-speed,  speed)

    override fun onCleared() {
        stopMotorLoop()
        wsClient.sendBrake()
        wsClient.disconnect(getApplication())
    }
}
