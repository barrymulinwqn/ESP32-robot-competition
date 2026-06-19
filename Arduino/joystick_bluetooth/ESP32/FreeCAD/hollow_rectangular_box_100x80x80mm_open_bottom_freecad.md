在 FreeCAD 中生成原生工程文件的方法：

1. 打开 FreeCAD。
2. 运行宏文件 `hollow_rectangular_box_100x80x80mm_open_bottom.FCMacro`。
3. 宏会创建一个外形为 100 x 80 x 80 mm、壁厚 2 mm、底面敞口的中空长方体。
4. 若运行环境允许保存，宏会自动在当前目录生成 `hollow_rectangular_box_100x80x80mm_open_bottom.FCStd`。
5. 如果没有自动保存，在 FreeCAD 中看到模型后手动执行“文件 -> 另存为”即可。

参数位于宏文件顶部，可直接修改：
- `OUTER_LENGTH = 100.0`
- `OUTER_WIDTH = 80.0`
- `OUTER_HEIGHT = 80.0`
- `WALL_THICKNESS = 2.0`
