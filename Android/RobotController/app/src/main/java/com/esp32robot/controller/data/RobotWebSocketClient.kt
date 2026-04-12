package com.esp32robot.controller.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.concurrent.TimeUnit

/**
 * RobotWebSocketClient
 *
 * 连接 ESP32-S3 WiFi AP（SSID: Robot-ESP32），通过 WebSocket 双向通信
 * 协议与 TB6612motor_ESP32S3_WiFi.ino 完全匹配：
 *   发送：{"cmd":"motor","a":<-255~255>,"b":<-255~255>}
 *         {"cmd":"laser","on":<true|false>}
 *         {"cmd":"brake"} {"cmd":"stop"} {"cmd":"ir_reset"} {"cmd":"get_ir"}
 *   接收：{"status":"connected","ip":"...","ssid":"..."}
 *         {"ir_count":<uint32>}
 */
class RobotWebSocketClient {

    // ── 连接状态 ──────────────────────────────────────────────────
    sealed class ConnectionState {
        object Disconnected : ConnectionState()
        object Connecting : ConnectionState()
        data class Connected(val ip: String) : ConnectionState()
        data class Error(val message: String) : ConnectionState()
    }

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _irCount = MutableStateFlow(0)
    val irCount: StateFlow<Int> = _irCount.asStateFlow()

    // ── OkHttp 客户端（单例，复用连接池）────────────────────────
    private val httpClient = OkHttpClient.Builder()
        .pingInterval(5, TimeUnit.SECONDS)   // WebSocket 保活心跳
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()

    private var webSocket: WebSocket? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null

    // ── WebSocket 事件监听 ────────────────────────────────────────
    private val wsListener = object : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) {
            // 握手成功，等待 ESP32 发送确认 JSON
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            try {
                val json = JSONObject(text)
                when {
                    json.has("ir_count") -> {
                        _irCount.value = json.getInt("ir_count")
                    }
                    json.optString("status") == "connected" -> {
                        val ip = json.optString("ip", "192.168.4.1")
                        _connectionState.value = ConnectionState.Connected(ip)
                    }
                }
            } catch (_: Exception) {
                // 忽略非 JSON 消息
            }
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            _connectionState.value = ConnectionState.Error(t.message ?: "连接失败")
        }

        override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
            _connectionState.value = ConnectionState.Disconnected
        }
    }

    /**
     * 连接 ESP32 WebSocket（先绑定 WiFi AP 网络，再建立 WS 连接）
     * Android 10+ 必须通过 ConnectivityManager 绑定到 ESP32 AP 网络，
     * 否则系统会把流量路由到移动数据网络
     */
    fun connect(
        context: Context,
        ssid: String = "Robot-ESP32",
        password: String = "12345678",
        wsUrl: String = "ws://192.168.4.1:81"
    ) {
        if (_connectionState.value is ConnectionState.Connecting ||
            _connectionState.value is ConnectionState.Connected
        ) return

        _connectionState.value = ConnectionState.Connecting

        val specifier = WifiNetworkSpecifier.Builder()
            .setSsid(ssid)
            .setWpa2Passphrase(password)
            .build()

        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .setNetworkSpecifier(specifier)
            .build()

        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

        networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                // 将进程绑定到 ESP32 AP 网络，后续 Socket 均走此路由
                cm.bindProcessToNetwork(network)
                // 网络就绪后建立 WebSocket
                openWebSocket(wsUrl)
            }

            override fun onLost(network: Network) {
                _connectionState.value = ConnectionState.Disconnected
                webSocket?.cancel()
                webSocket = null
            }

            override fun onUnavailable() {
                _connectionState.value = ConnectionState.Error("找不到 $ssid 热点，请检查 ESP32 是否已开机")
            }
        }

        cm.requestNetwork(request, networkCallback!!)
    }

    private fun openWebSocket(url: String) {
        val request = Request.Builder().url(url).build()
        webSocket = httpClient.newWebSocket(request, wsListener)
    }

    // ── 指令发送 ─────────────────────────────────────────────────

    /** 差速电机控制：a = 左电机 -255~255，b = 右电机 -255~255 */
    fun sendMotor(a: Int, b: Int) {
        val ca = a.coerceIn(-255, 255)
        val cb = b.coerceIn(-255, 255)
        send("""{"cmd":"motor","a":$ca,"b":$cb}""")
    }

    /** 激光开关 */
    fun sendLaser(on: Boolean) = send("""{"cmd":"laser","on":$on}""")

    /** 短路制动急停（竞赛首选） */
    fun sendBrake() = send("""{"cmd":"brake"}""")

    /** 惰行停止 */
    fun sendStop() = send("""{"cmd":"stop"}""")

    /** 清零 IR 计数器 */
    fun sendIrReset() = send("""{"cmd":"ir_reset"}""")

    /** 主动请求当前 IR 计数 */
    fun sendGetIr() = send("""{"cmd":"get_ir"}""")

    private fun send(json: String): Boolean = webSocket?.send(json) == true

    /** 断开连接并释放网络绑定 */
    fun disconnect(context: Context) {
        webSocket?.close(1000, "User disconnect")
        webSocket = null
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        networkCallback?.let { cm.unregisterNetworkCallback(it) }
        networkCallback = null
        cm.bindProcessToNetwork(null)   // 解除网络绑定，恢复正常路由
        _connectionState.value = ConnectionState.Disconnected
    }
}
