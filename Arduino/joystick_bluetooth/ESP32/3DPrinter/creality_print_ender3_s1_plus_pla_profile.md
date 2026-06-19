# Creality Print 参数表

适用对象：
- 打印机：Creality Ender-3 S1 Plus
- 切片软件：Creality Print
- 材料：PLA
- 模型：`hollow_rect_100_80_80.stl`
- 模型特征：100 x 80 x 80 mm，中空，壁厚 2 mm，底部敞口

## 导入模型后先确认
- 模型开口朝下
- 模型底部贴在热床平面
- 缩放比例为 100%
- 单位为 mm

## 推荐参数

### Quality
- Layer Height：0.20 mm
- Initial Layer Height：0.24 mm
- Line Width：0.40 mm
- Wall Line Width：0.40 mm
- Top Surface Line Width：0.40 mm

### Shell
- Wall Line Count：5
- Top Layers：5
- Bottom Layers：0
- Top Thickness：1.0 mm
- Bottom Thickness：0 mm
- Optimize Wall Printing Order：On

说明：
这个模型本身就是底部敞口，所以 `Bottom Layers` 必须保持 `0`，避免切片器把底面封死。

### Infill
- Infill Density：0%
- Infill Pattern：Grid 或 Lines 均可

说明：
由于模型是 2 mm 薄壁中空盒体，主要依靠壁厚成型，不需要填充。

### Speed
- Print Speed：50 mm/s
- Wall Speed：40 mm/s
- Outer Wall Speed：30 mm/s
- Top Surface Speed：25 mm/s
- Initial Layer Speed：20 mm/s
- Travel Speed：120 mm/s

### Material
- Printing Temperature：205 C
- Initial Layer Printing Temperature：210 C
- Build Plate Temperature：60 C
- Initial Layer Build Plate Temperature：60 C

### Cooling
- Enable Print Cooling：On
- Fan Speed：100%
- Initial Fan Speed：0%
- Regular Fan Speed at Height：0.60 mm
- Regular Fan Speed at Layer：3

### Retraction
- Enable Retraction：On
- Retraction Distance：0.8 mm
- Retraction Speed：40 mm/s
- Z Hop When Retracted：On
- Z Hop Height：0.2 mm
- Combing Mode：Not in Skin 或 Within Infill

说明：
Ender-3 S1 Plus 的 Sprite 直驱挤出机打印 PLA 时，`0.8 mm` 回抽是比较稳的起始值。

### Build Plate Adhesion
- Build Plate Adhesion Type：Brim
- Brim Width：6 mm
- Brim Line Count：10 到 12

说明：
这个模型实际接触热床的是一圈 2 mm 厚边框，建议加 `Brim` 降低翘边风险。

### Support
- Generate Support：Off

说明：
模型开口朝下摆放时，不需要支撑。

## Creality Print 中最重要的 4 个点
- `Bottom Layers = 0`
- `Infill Density = 0%`
- `Wall Line Count = 5`
- `Build Plate Adhesion = Brim`

## 如果打印失败，优先这样调

### 翘边
- Build Plate Temperature 改到 65 C
- Brim Width 改到 8 mm
- Initial Layer Speed 改到 15 mm/s

### 拉丝
- Printing Temperature 改到 200 C
- Retraction Distance 改到 1.0 mm
- Retraction Speed 改到 45 mm/s

### 层间结合差
- Printing Temperature 改到 210 C
- Fan Speed 改到 80%
- Print Speed 改到 45 mm/s

## 最省事的一套最终建议
- Layer Height：0.20 mm
- Wall Line Count：5
- Top Layers：5
- Bottom Layers：0
- Infill Density：0%
- Print Speed：50 mm/s
- Outer Wall Speed：30 mm/s
- Printing Temperature：205 C
- Bed Temperature：60 C
- Retraction：0.8 mm / 40 mm/s
- Fan：第 3 层后 100%
- Adhesion：Brim 6 mm
- Support：Off
