# Android 开发环境搭建、打包与安装指南

> 目标：从零搭建 Android 开发环境，编译 **RobotController** App，安装到手机进行测试

---

## 一、环境要求

| 工具 | 版本要求 | 说明 |
|------|----------|------|
| macOS / Windows / Linux | 任意 | 开发宿主机 |
| JDK (Java Development Kit) | **17**（推荐 Temurin） | Kotlin/Gradle 编译依赖 |
| Android Studio | **Hedgehog 2023.1.1** 或更新 | 官方 IDE，含 SDK Manager |
| Android SDK | compileSdk **34**，minSdk **29** | 由 Android Studio 自动安装 |
| Android 手机 | Android 10+（API 29+） | 用于真机测试 |

---

## 二、安装 JDK 17

### macOS（推荐 Homebrew）

```bash
# 安装 Homebrew（如已有则跳过）
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装 Eclipse Temurin JDK 17
brew install --cask temurin@17

# 验证
java -version
# 预期输出：openjdk version "17.x.x" ...
```

### Windows

1. 访问 https://adoptium.net/
2. 选择 **JDK 17 (LTS)** → **Windows x64 MSI Installer**
3. 安装时勾选 **"Set JAVA_HOME variable"**
4. 打开 PowerShell 验证：`java -version`

---

## 三、安装 Android Studio

1. 访问 https://developer.android.com/studio
2. 下载对应平台安装包（.dmg / .exe / .tar.gz）
3. 按向导安装，首次启动时选择 **Standard 安装**（自动下载 Android SDK、模拟器等）

### 安装完成后配置 SDK

打开 Android Studio → **Settings（偏好设置）→ Languages & Frameworks → Android SDK**

- **SDK Platforms** 标签页：勾选 **Android 14.0 (API 34)**，点击 Apply
- **SDK Tools** 标签页：确认以下已安装：
  - Android SDK Build-Tools 34.x
  - Android SDK Platform-Tools
  - Android Emulator（可选，真机调试可不装）

---

## 四、打开项目

1. 启动 Android Studio
2. 选择 **Open**（不是 New Project）
3. 导航到：`ESP32-robot-competition/Android/RobotController`
4. 点击 **OK**，等待 Gradle 同步（首次约 3~10 分钟，需下载依赖）

> **Gradle 同步失败常见原因：**
> - 网络问题无法下载依赖 → 配置代理或使用国内镜像（见附录）
> - JDK 版本不对 → File → Project Structure → SDK Location → JDK 选 17

---

## 五、连接 Android 手机（USB 调试）

### 手机端操作

1. 进入 **设置 → 关于手机**，连续点击 **版本号** 7 次，开启开发者模式
2. 进入 **设置 → 开发者选项**，开启 **USB 调试**
3. 用 USB 数据线连接电脑，弹出提示时选择 **"允许 USB 调试"**

### 电脑端验证

```bash
# 安装 platform-tools 后执行
adb devices
# 预期输出：
# List of devices attached
# XXXXXXXX    device
```

若显示 `unauthorized`，检查手机上的授权弹窗。

---

## 六、真机运行（调试安装）

### 方法 A：Android Studio 一键运行

1. Android Studio 工具栏顶部选择目标设备（应显示你的手机型号）
2. 点击绿色 **▶ Run** 按钮（或 `Shift+F10`）
3. Gradle 自动构建 debug APK → adb 安装 → 启动 App

### 方法 B：命令行

```bash
cd ESP32-robot-competition/Android/RobotController

# macOS/Linux
./gradlew installDebug

# Windows
gradlew.bat installDebug
```

构建成功后 APK 自动安装到已连接手机，App 自动启动。

---

## 七、打包 Release APK（正式发布）

### 1. 生成签名密钥（只需一次）

Android Studio → **Build → Generate Signed Bundle / APK**

1. 选择 **APK**
2. 点击 **Create new key store**，填写：
   - Key store path：选择保存位置（如 `~/robot_key.jks`）
   - Password：设置密钥库密码（务必记住！）
   - Alias：`robot_key`
   - Key Password：设置密钥密码
   - Validity：25（年）
   - 填写 Certificate 信息（Country 填 CN）
3. 点击 **OK** 生成密钥文件

### 2. 配置签名（可选，自动化打包用）

在 `app/build.gradle.kts` 的 `android {}` 块中添加：

```kotlin
signingConfigs {
    create("release") {
        storeFile = file("${System.getProperty("user.home")}/robot_key.jks")
        storePassword = System.getenv("KEY_STORE_PASSWORD") ?: ""
        keyAlias = "robot_key"
        keyPassword = System.getenv("KEY_PASSWORD") ?: ""
    }
}
buildTypes {
    release {
        isMinifyEnabled = true
        signingConfig = signingConfigs.getByName("release")
        proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
    }
}
```

### 3. 构建 Release APK

```bash
# 命令行构建（推荐竞赛前最终打包）
./gradlew assembleRelease

# APK 输出路径：
# app/build/outputs/apk/release/app-release.apk
```

---

## 八、手机安装 APK（不通过 USB）

适用场景：竞赛现场快速分发给队友

### 方法 A：ADB 直接安装

```bash
adb install app/build/outputs/apk/release/app-release.apk
```

### 方法 B：文件传输安装

1. 将 APK 文件复制到手机存储（微信、QQ、AirDrop、USB 均可）
2. 手机文件管理器找到 APK，点击安装
3. 若提示"未知来源"，进入 **设置 → 安全 → 安装未知应用** → 允许对应文件管理器安装

### 方法 C：二维码分发（推荐竞赛团队）

```bash
# 先启动本地 HTTP 服务
cd app/build/outputs/apk/release/
python3 -m http.server 8888

# 手机同 WiFi 下访问 http://<电脑IP>:8888/app-release.apk 下载安装
```

---

## 九、App 使用流程

```
1. 确认 ESP32-S3 已上电，WiFi AP 热点"Robot-ESP32"已广播
2. 打开 RobotController App
3. 点击界面中央的「连接」按钮
   → Android 系统弹出对话框：是否连接到"Robot-ESP32"？点击「连接」
4. 状态指示灯变绿，显示"已连接 192.168.4.1"
5. 左摇杆控制左电机（上推前进/下推后退）
   右摇杆控制右电机（坦克差速转向）
6. 橙色闪电按钮：激光发射器开/关
7. 红色停止按钮：急停（短路制动）
8. 蓝色刷新按钮：重置 IR 命中计数
```

---

## 十、常见问题

### Q1：Gradle 同步时 "Could not resolve com.squareup.okhttp3:okhttp:4.12.0"

**原因**：网络无法访问 Maven Central

**解决**：在 `settings.gradle.kts` 的 `repositories {}` 中添加阿里云镜像：

```kotlin
maven { url = uri("https://maven.aliyun.com/repository/central") }
maven { url = uri("https://maven.aliyun.com/repository/google") }
```

### Q2：App 点击连接后一直"正在连接..."

**排查步骤**：
1. 检查 ESP32 串口监视器，确认 `[WiFi] AP started` 已打印
2. 手机 WiFi 列表中确认能看到 `Robot-ESP32`
3. 检查 AndroidManifest.xml 中 `ACCESS_FINE_LOCATION` 权限是否已授予
4. Android 12+ 需额外检查位置权限：**设置 → 权限 → 位置信息 → RobotController → 使用 App 时允许**

### Q3：编译报错 "Kotlin version mismatch"

```bash
# 清理缓存后重新编译
./gradlew clean assembleDebug
```

### Q4：`adb devices` 显示设备但 Android Studio 不识别

```bash
# 重启 adb server
adb kill-server
adb start-server
adb devices
```

---

## 附录：项目文件结构

```
Android/RobotController/
├── build.gradle.kts                     # 根 Gradle 配置
├── settings.gradle.kts                  # 项目设置（模块声明）
├── gradle/wrapper/
│   └── gradle-wrapper.properties        # Gradle 版本 8.6
└── app/
    ├── build.gradle.kts                 # App 模块依赖配置
    ├── proguard-rules.pro               # 混淆规则
    └── src/main/
        ├── AndroidManifest.xml          # 权限 + Activity 声明
        ├── java/com/esp32robot/controller/
        │   ├── MainActivity.kt          # 入口 Activity，全屏横屏
        │   ├── data/
        │   │   └── RobotWebSocketClient.kt   # OkHttp WebSocket + WiFi AP 绑定
        │   ├── viewmodel/
        │   │   └── RobotViewModel.kt    # 50Hz 电机循环 + 状态管理
        │   └── ui/
        │       ├── ControlScreen.kt     # Compose 主界面（双摇杆布局）
        │       ├── theme/
        │       │   └── Theme.kt         # 深色主题
        │       └── components/
        │           ├── JoystickView.kt          # 原生 View 摇杆（零延迟）
        │           └── JoystickComposable.kt    # Compose 包装
        └── res/
            ├── values/strings.xml
            ├── values/colors.xml
            ├── values/themes.xml
            └── drawable/ic_launcher_foreground.xml
```
