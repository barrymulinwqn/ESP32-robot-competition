package com.esp32robot.controller

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.Composable
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.esp32robot.controller.ui.ControlScreen
import com.esp32robot.controller.ui.theme.RobotControllerTheme
import com.esp32robot.controller.viewmodel.RobotViewModel

class MainActivity : ComponentActivity() {

    private val viewModel: RobotViewModel by viewModels()

    // 申请精确定位权限（Android 10+ 连接指定 SSID 必须）
    private val locationPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) {
                viewModel.connect()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 全屏沉浸式（竞赛场景去除状态栏/导航栏，最大化可视区域）
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
        // 保持屏幕常亮（竞赛中不能熄屏）
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        setContent {
            RobotControllerTheme {
                ControlScreen(viewModel = viewModel)
            }
        }
    }

    override fun onStop() {
        super.onStop()
        // App 进入后台时制动停车，防止失控
        viewModel.brake()
    }

    override fun onDestroy() {
        super.onDestroy()
        viewModel.disconnect()
    }
}
