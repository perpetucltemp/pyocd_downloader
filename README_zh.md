# own_pyocd — 面向 STM32 / HK32 / GD32 的 pyOCD 图形化烧录工具

这是一个轻量级的 Windows 图形界面，封装了自带的
[`pyocd`](https://github.com/pyocd/pyOCD) 命令行工具。它可以**连接 MCU、整片或按扇区擦除、
烧录固件（BIN/HEX/ELF）、复位运行**，且不需要安装 Python / conda 环境。
`pyocd.exe` 及其 `_internal/` 运行环境随工具一起发布，唯一的外部依赖是设备的
`.pack` 文件。

## 功能特性

- **连接 / 断开**：通过 DAP 调试探针（SWD/JTAG）连接目标芯片。
- **自动获取型号列表**：从自带的 Keil / GigaDevice / HKMicroChip 的 DFP
  `.pack` 文件中解析出可烧录的 MCU 型号（`pyocd list --targets --pack ...`）。
- **探针自动识别**：连接多个探针时弹出选择框。
- **擦除**：
  - 整片擦除（`pyocd erase --chip`）。
  - **按扇区擦除**：根据设备包（PDSC）解析出的 Flash 布局，弹出图形化扇区选择框。
  - 加载固件后，自动勾选固件覆盖到的扇区。
- **下载（烧录）**：
  - 格式支持：BIN（需填基地址）、HEX、ELF（地址从文件自身读取）。
  - 烧录前提供四种擦除策略：
    - `不擦除` —— 直接写入（目标区域须已擦除）。
    - `按固件地址自动擦除` —— 只擦除固件覆盖到的扇区。
    - `擦除选中扇区` —— 擦除扇区选择框里勾选的扇区。
    - `整片擦除后下载` —— `pyocd load -e chip`。
  - 烧录后可选「复位运行」。
- **扇区选择对话框**：列出每个 Flash 扇区（地址范围 + 大小），支持全选 / 全不选 /
  反选，并根据固件自动勾选（HEX/ELF 按文件地址，BIN 按基地址 + 文件大小）。
- **命令队列**：下载需要预擦除时，先依次执行擦除命令，再自动执行下载命令；
  任一步失败则中止后续步骤。
- **日志面板**：实时显示 pyocd 输出，可清空 / 保存；带进度条与状态显示。

## 目录结构

```
own_pyocd/
├── README.md                 # 英文说明
├── README_zh.md              # 本文件（中文）
├── pyocd_cli.bat             # 可选：直接调用 pyocd.exe 的命令行入口
├── pyocd-windows-0.46.0/     # 自带的 pyocd 发行包（exe + _internal/）
│   └── pyocd.exe
├── packs/                    # 设备 DFP 包（*.pack），运行时自动扫描
│   ├── Keil.STM32F4xx_DFP.3.1.1.pack
│   ├── GigaDevice.GD32F4xx_DFP.3.5.0.pack
│   └── HKMicroChip.HK32C030xx_DFP.1.0.5.pack
└── pyocd_ui/                 # Qt6 图形界面（本仓库源码）
    ├── main.cpp
    ├── mainwindow.{h,cpp}
    ├── PyOcdRunner.{h,cpp}    # 封装 pyocd.exe 的 QProcess 调用
    ├── PyocdLocator.{h,cpp}   # 定位自带的 pyocd.exe
    ├── PackParser.{h,cpp}     # 解析 .pack PDSC -> Flash 布局 / 扇区表
    ├── FirmwareParser.{h,cpp} # 解析 BIN/HEX/ELF -> 地址范围
    ├── SectorSelectorDialog.{h,cpp}
    ├── build.bat             # 一键 Windows 构建
    ├── CMakeLists.txt
    └── packaging/            # deploy.ps1 + WiX 安装包脚本
```

## 依赖环境

| 组件            | 版本 / 说明                                  |
|-----------------|---------------------------------------------|
| Qt              | Qt 6（Widgets 模块）                         |
| 编译器          | MinGW-w64（GCC）                             |
| 构建系统        | CMake ≥ 3.16 + Ninja                         |
| pyocd（自带）   | `pyocd-windows-0.46.0`（已包含）             |
| 设备包          | 任意 Keil / GigaDevice / HKMicroChip 的 DFP `.pack` |

> `pyocd.exe` 及其 Python 运行环境属于**第三方组件**，按设计不纳入源码仓库。
> 参见下文「打包 pyocd」。

## 获取自带的 pyocd

界面要求 `pyocd_ui/` 同级存在一个名为 `pyocd-windows-<版本>` 的目录，内含
`pyocd.exe` 与 `_internal/`。从
[pyOCD 发布页](https://github.com/pyocd/pyOCD/releases) 下载官方
"standalone windows" 版本并解压，确保目录结构为：

```
own_pyocd/pyocd-windows-0.46.0/pyocd.exe
own_pyocd/pyocd-windows-0.46.0/_internal/...
```

（若放在其他位置，可设置 `PYOCD_DIR` 或调整 `PyocdLocator`。）

## 构建（Windows）

环境变量可覆盖默认工具链路径（`build.bat` 默认写的是 `D:\Software_Installed\...`，
请改成你自己的路径）：

```bat
set MINGW_DIR=D:\path\to\mingw64\bin
set QT_DIR=D:\path\to\qt_install
build.bat            rem 增量构建
build.bat clean      rem 清理并重新构建
build.bat nobuild    rem 仅配置不编译
```

脚本会执行 `qt-cmake -B build -G Ninja`、`ninja` 编译，然后运行
`windeployqt`，使 `build/` 目录可独立运行。产物为 `build/pyocd_ui.exe`。

若要生成安装包，使用 `pyocd_ui/packaging/deploy.ps1`（基于 WiX），它还会把
`packaging/runtime-deps/` 下的第三方运行库（例如 `Qt6Core.dll` 依赖的 `zstd.dll`）
一并复制进去。

## 使用步骤

1. **选择 MCU 型号**：从下拉框选择（由设备包解析得到）。列表为空时点「刷新」。
2. **选择探针**：或保持「自动」自动识别。点「连接」。工具会执行
   `commander status` 并读取向量表，确认连接并提示 Flash 是否为空。
3. **加载固件**：选择 `.bin`/`.hex`/`.elf` 文件。BIN 还需填写基地址（如 `0x08000000`）。
4. **擦除**（下载前可选）：
   - *整片擦除* —— 确认后擦除全部。
   - *按扇区擦除* —— 点**选择扇区…**打开扇区对话框，固件覆盖的扇区会被自动勾选。
5. **下载**：选择「下载擦除」策略，点**下载固件**。若需要预擦除，工具会先擦除选中
   扇区，再自动烧录——整个过程在一个命令队列中完成。
6. **复位运行**（如勾选）启动程序。

### 按扇区擦除的实现细节

`pyocd erase --sector 0x08000000-0x08003FFF` **每次只接受一个地址范围**，因此多选的
扇区会被拆成**多条独立的擦除命令**（排队执行），而不是一条带多个 `--sector` 的命令。

### 下载擦除策略对照

| 模式                | pyocd 行为                                      |
|---------------------|-------------------------------------------------|
| 不擦除              | `load` 不擦除（区域须先擦过）                    |
| 按固件地址自动擦除  | 只擦覆盖扇区，再 `load -e sector`                |
| 擦除选中扇区        | 擦选中扇区，再 `load -e sector`                  |
| 整片擦除后下载      | `load -e chip`（由 pyocd 内部整片擦）            |

## 支持的设备

凡是自带 `.pack` 中 PDSC 暴露了 Flash 区域的 MCU 均可。已验证系列包括 STM32F4、
GD32F4、HK32C030。Flash 布局 / 扇区表由设备包推导；未知型号使用默认的 2 KB 扇区。

## 注意事项 / 故障排查

- **目标机器提示缺少 `zstd.dll`**：它是 `Qt6Core.dll` 的间接依赖。`deploy.ps1` 会从
  `packaging/runtime-deps/` 打包该文件，请确保安装包里它位于可执行文件旁边。
- **"unrecognized arguments" / `-e none` 报错**：`pyocd load -e` 只接受
  `auto|chip|sector`，本工具不会传入 `none`。
- **连接多个探针**：界面会弹出选择框，而不是自行猜测。

## 许可证

见 `LICENSE`。（第三方组件——pyocd、Qt、设备包、MinGW——各自保留其许可证。）
