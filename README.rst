==============================
The Ren'Py Visual Novel Engine
==============================

https://www.renpy.org

ShinkiEMU adapter
================

``shinki/Cargo.toml`` builds the Windows x64 and Linux x64 official-SDK
adapter with Rust 1.99.1 and edition 2024. The upstream baseline is release
``8.5.3.26051504`` (``39895c1e017f0b36ffea2447d97eccd69d76ee1c``).
``shinki/sdk.json`` pins the matching official SDK archive and native runtime
hashes; ShinkiEMU's ``build/dependencies.toml`` must use the same pins.
Prepare and build from ShinkiEMU with ``cargo xtask deps --core renpy
--target <triple>`` and ``cargo xtask build --core renpy --target <triple>
--release``. Linux uses ``x86_64-unknown-linux-gnu`` and Windows uses
``x86_64-pc-windows-msvc``.

The Linux adapter and display extension link the SDK's ``librenpython.so``;
the SDK combines CPython, SDL and native Ren'Py modules in that library.
Their install RPATHs resolve it beside the packaged core, and the display
override uses the native ``.so`` extension suffix. ShinkiEMU prepares a
task-owned build interpreter from pinned CPython and Cython sources on Linux.
Upstream script execution, rendering and save resolvers are unchanged.

The macOS adapter links the verified universal SDK runtime at
``lib/py3-mac-universal/librenpython.dylib``. The adapter and display
extension also build as universal x86_64/arm64 dylibs. Packaging preserves
both runtime slices and uses ``@loader_path/Resources`` for the core and
``@loader_path`` for the display extension.

The ``static`` adapter feature builds a real archive and builtin display
override, exporting ``shinki_emu_renpy_query`` instead of the shared plugin
entrypoint. ``RENPY_STATIC_ROOT`` selects target-built CPython headers under
``include/python3.12``, native archives under ``lib`` and the system-library
closure in ``lib/pkgconfig/shinki-renpy-runtime.pc``. It never substitutes
the desktop runtime for a mobile target. Before registering the embedded
provider, the host calls ``set_resource_root`` with the packaged
``cores/renpy/Resources`` directory; changing it after interpreter startup
is rejected. Python sources, the bridge and licenses remain recorded in
the native-static build manifest for Manager packaging.

Android runtime sources
-----------------------

``shinki/android_runtime.py`` builds the real CPython, Ren'Py builtin modules,
SDL2 and native dependency archives from official ``renpy-build`` revision
``7bfab40c1174f622f644b24669afd5fb167fbb79`` and Ren'Py source revision
``39895c1e017f0b36ffea2447d97eccd69d76ee1c``. Use a dedicated Ubuntu 24.04
build root provisioned with the upstream build prerequisites, Cython 3.2.4,
NDK r29's Linux archive and the official ``CubismSdkForNative-4-r.6.2.zip``
header SDK in ``tars``. ``shinki/sdk.json`` records their URLs and SHA256
checksums. The script verifies both source revisions and both SDK archive
checksums and limits native jobs to three::

    python shinki/android_runtime.py --build-root /path/to/renpy-build \
        --target aarch64-linux-android --output /path/to/runtime-arm64

Run again with ``x86_64-linux-android`` and a different output directory.
Each output supplies ``RENPY_STATIC_ROOT``, the target standard library,
its CPython license and the Cubism header SDK's license notice.
``runtime.json`` records the source pins and archive hashes. Existing outputs
are preserved. The Cubism Core implementation is not linked or copied:
upstream loads a game-supplied library at runtime. Its proprietary license
remains separate from Ren'Py's license.

This private SDL2 build uses an EGL pbuffer on Android's default display,
not an SDLActivity window. Audio retains native OpenSL ES output and uses
SDL's pthread priority path without calling an absent SDLActivity Java API.
The adapter selects GLES2 on Android. Native C and C++ symbols are renamed
throughout the runtime archives, with a matching C header for adapter/display
compilation, separating SDL2, its codecs and HarfBuzz from other cores.
The staged Python packages come from the exact source/wheel inventory in
``licenses/renpy/python-sources.json``; host-native acceleration modules are
not copied into Android resources.
The desktop SDK and RAPT shared libraries are not Android static inputs.
Target-specific CPython sysconfig data lives under
``Resources/lib/<target>/python3.12`` and precedes the common standard
library on the interpreter path. Development ``config-*`` archives are not
runtime assets; common files are byte-identical across the universal APK.
The recipe runner builds the full native closure through ``librenpy``.
It excludes RAPT's standalone Java/APK packaging rows because Manager owns
NativeActivity and packaging; no native compile task is replaced or skipped.
Inventoried Python downloads stay under the recipe's ignored ``tmp/`` build
directory, so preparing the second ABI keeps the pinned source checkout clean.



Branches
========

The following branches are the most interesting.

``fix``
    The fix branch is used for fixes to the current version of Ren'Py that do
    not require dangerous changes. The fix branch is also the source of the
    documentation on https://www.renpy.org/. This branch is automatically
    merged into master on a regular basis.

    Pull requests that contain fixes or documentation improvements should be
    made to the fix branch. When a release is made, the master branch is
    copied to the fix branch.

``master``
    The master branch is where the main focus of development is. This branch
    will eventually become the next release of Ren'Py.

    Pull requests that contain new features, that require incompatible changes,
    or major changes to Ren'Py's internals should be targeted at the master
    branch.


Getting Started
===============

Ren'Py depends on a number of Python modules written in Cython and C. For
changes to Ren'Py that only involve Python modules, you can use the modules
found in the latest nightly build. Otherwise, you'll have to compile the
modules yourself.

The development scripts assume a POSIX-like platform. The scripts should run
on Linux or macOS, and can be made to run on Windows using an environment
like MSYS.

Nightly Build
-------------

Nightly builds can be downloaded from:

   https://nightly.renpy.org

Note that the latest nightly build is at the bottom of the list. Once you've
unpacked the nightly, change into this repository, and run::

    ./after_checkout.sh <path-to-nightly>

Once this script completes, you should be able to run Ren'Py using renpy.sh,
renpy.app, or renpy.exe, as appropriate for your platform.

If the current nightly build doesn't work, please wait 24 hours for a new
build to occur. If that build still doesn't work, contact Tom (`pytom at bishoujo.us`,
or @renpytom on Twitter/X) to find out what's wrong.

The ``doc`` symlink will dangle until documentation is built, as described
below.

Compiling the Modules
----------------------

Building the modules requires you have the many dependencies installed on
your system. On Ubuntu and Debian, these dependencies can be installed with
the command::

    sudo apt install python3-dev libassimp-dev libavcodec-dev libavformat-dev \
        libswresample-dev libswscale-dev libharfbuzz-dev libfreetype6-dev libfribidi-dev libsdl2-dev \
        libsdl2-image-dev libsdl2-gfx-dev libsdl2-mixer-dev libsdl2-ttf-dev libjpeg-dev pkg-config

Ren'Py requires SDL_image 2.6 or greater. If your distribution doesn't include
that version, you'll need to download it from:

    https://github.com/libsdl-org/SDL_image/tree/SDL2

We strongly suggest using a package manager to create virtual environment and
manage dependencies. We have tested with `uv <https://docs.astral.sh/uv/>`_ but
other package managers should work as well. To create a virtual environment and
install dependencies, open a new terminal and run::

    uv sync

After that, compile extension modules and run Ren'Py using the command::

    ./run.sh


Other Platforms
---------------

Where supported, Ren'Py will attempt to find include directories and library paths
using pkg-config. If pkg-config is not present, include and library paths can be
specified using CFLAGS and LDFLAGS.

If RENPY_CFLAGS is present in the environment and CFLAGS is not, setup.py
will set CFLAGS to RENPY_CFLAGS. The same is true for RENPY_LDFLAGS,
RENPY_CC, RENPY_CXX, and RENPY_LD.

Setup.py does not support cross-compiling. See https://github.com/renpy/renpy-build
for software that cross-compiles Ren'Py for many platforms. The renpy-build system
also include some runtime components for Android, iOS, and web.


Documentation
=============

Building
--------

Building the documentation requires Ren'Py to work. You'll either need to
link in a nightly build, or compile the modules as described above. You'll
also need the `Sphinx <https://www.sphinx-doc.org>`_ documentation generator.
If you have pip working, install Sphinx using::

    pip install -U sphinx sphinx_rtd_theme sphinx_rtd_dark_mode sphinx-tabs

Once Sphinx is installed, change into the ``sphinx`` directory inside the
Ren'Py checkout and run::

    ./build.sh

Format
------

Ren'Py's documentation consists of reStructuredText files found in sphinx/source, and
generated documentation found in function docstrings scattered throughout the code. Do
not edit the files in the ``sphinx/source/inc`` folder directly, as they will be
overwritten.

Docstrings may include tags on the first few lines:

\:doc: `section` `kind`
    Indicates that this function should be documented. `section` gives
    the name of the include file the function will be documented in, while
    `kind` indicates the kind of object to be documented (one of ``function``,
    ``method`` or ``class``. If omitted, `kind` will be auto-detected.
\:name: `name`
    The name of the function to be documented. Function names are usually
    detected, so this is only necessary when a function has multiple aliases.
\:args: `args`
    This overrides the detected argument list. It can be used if some arguments
    to the function are deprecated.

For example::

    def warp_speed(factor, transwarp=False):
        """
        :doc: warp
        :name: renpy.warp_speed
        :args: (factor)

        Exceeds the speed of light.

        `factor`
            The warp factor. See Sternbach (1991) for details.

        `transwarp`
            If True, use transwarp. This does not work on all platforms.
        """

        renpy.engine.warp_drive.engage(factor)


Translating
===========

For best practices when it comes to translating the launcher and template
game, please read:

https://lemmasoft.renai.us/forums/viewtopic.php?p=321603#p321603


Contributing
============

For bug fixes, documentation improvements, and simple changes, just
make a pull request. For more complex changes, it might make sense
to file an issue first so we can discuss the design.

License
=======

For the complete licensing terms, please read:

https://www.renpy.org/doc/html/license.html


ShinkiEMU 原生接入
================

``shinki`` 分支基于官方 ``8.5.3.26051504``，上游提交
``39895c1e017f0b36ffea2447d97eccd69d76ee1c``。适配包为
``shinki-emu-renpy``，plugin/family ID 均为 ``renpy``，当前构建目标为
Windows x64，导出 ShinkiEMU C ABI v3。

执行、Screen Language、ATL、GL2、音视频、回滚及存档全部使用原 Ren'Py。
不另写 ``.rpy/.rpyc`` 解释器，也不启动游戏子进程。官方 CPython 3.12.7
及编译扩展保持在同一进程、同一原生工作线程；解释器随模块常驻，关闭游戏
使用官方清理与 ``reload_all``，不反复 finalize/reinitialize Cython 模块。

只重编译必要的 ``renpy.pygame.display``：SDL GL backing window 从创建起
保持隐藏，不在核心中 swap。最终 GL2 back buffer 经原 ``uguu.glReadPixels``
直接写入四槽原生帧池，CPU lease 独立于 Python、窗口及 session 生存。
Manager 是最终呈现者。输入进入同一 SDK 的 SDL 状态与事件队列；暂停复用
原生全局音视频暂停，并扣除游戏时钟中的暂停时长。

构建
----

需要 MSVC x64、CMake/Ninja、构建用 Python 3.11+ 与 Cython 3.2.4、CPython
3.12.7 开发头、SDL 2.0.20 头，以及官方 Ren'Py 8.5.3 SDK。源码与原生 SDK
必须匹配；``shinki/sdk.json`` 记录来源、ZIP 校验和及五个原生 DLL 的固定
SHA256，构建入口拒绝不匹配的原生库。MSVC 导入库由官方 MinGW DLL 的
导出生成，不加载第二份 Python 或 SDL。

在 MSVC x64 Developer Command Prompt 中进入 ShinkiEMU 仓库后执行
（Ninja 可避免 MSBuild 的深层中间文件路径限制）::

    set CMAKE_GENERATOR=Ninja
    cargo xtask deps --target x86_64-pc-windows-msvc --core renpy
    cargo xtask build --target x86_64-pc-windows-msvc --core renpy --release

独立 Cargo 入口为 ``shinki/Cargo.toml``。``build.rs`` 验证官方 DLL 的
SHA256，从 DLL 导出生成 MSVC 导入库，再运行 CMake/Cython 编译和安装。
原生构建、安装目录与 ``core-build.json`` 都位于 Cargo 的 ``OUT_DIR``；
构建脚本不联网、不安装工具，也不递归调用 Cargo。

直接构建 adapter 时，调用者须提供 ``RENPY_SDK_ROOT``、
``RENPY_PYTHON_DEV``、``RENPY_SDL_INCLUDE``、``Python3_EXECUTABLE`` 和
``RENPY_CYTHON_LICENSE``。分别指向固定版本的 SDK、含
``include/Python.h`` 与 ``LICENSE.txt`` 的 CPython 开发根、含 ``SDL.h``
的 SDL 头目录、装有 Cython 3.2.4 的构建解释器，以及该 Cython 源发行版的
``LICENSE.txt``（同目录还须有 ``COPYING.txt``）。缺少依赖会直接报错。
CMake 原生构建最多使用四个并行任务。

``core-build.json`` 指向真实 ``shinki_emu_renpy.dll``，并记录完整
``Resources/`` 与 Ren'Py、CPython、Cython、pthread、adapter 的许可。
官方五个运行时 DLL 安装在核心旁，由打包工具收集原生依赖；``Resources/``
包含本 fork 的 Python/common 资源、官方标准库、桥接脚本和重编译的
``overrides/display.pyd``，不能只复制核心 DLL。
Host 必须使用 ``shinki.module_owner`` v1 扩展。

运行与资源转换
--------------

唯一配置 ``data_directory`` 指定私有数据根；留空使用当前用户的本地应用
数据目录。核心按规范游戏路径分配独立子目录，保存、日志和生成缓存都不写回
原安装。载入失败保留原资源，不从原游戏存档目录合并数据。

核心保留官方 RPA 索引格式。发行版自定义封装通过 ShinkiEMU 的显式 Luau
离线补丁转换为标准资源；本核心没有索引列顺序配置、游戏名判断或替代
archive loader。转换产物使用官方路径读取，播放时不加载转换脚本。

公开 ``shinki/fixtures/smoke`` 项目走生产 C ABI 验证了菜单、鼠标/键盘、
Unicode 输入、原生存读档、ATL 暂停恢复、初始挂起、关闭后 lease、
同进程重开、owner 重建，以及阻塞窗口请求取消后再次打开。
这些结果不等于所有 Ren'Py 版本、第三方扩展或完整游戏路线均兼容。

真实 HimaNatsu 8.5.2 发行版经显式 Luau 归一化后，已验证内置简体中文、
主菜单、连续正文、原生保存/读回后继续、暂停恢复、同进程重开后再次读档，
以及与 640×360 项目双向切换。实际游戏逻辑画布为 2560×1440，交付
1280×720 完整帧；原安装 1443 个文件及 49 个原存档/缓存内容均未改变。
未验全路线、结局、完整影片或人工听音，不外推其它 Ren'Py 版本。

常驻解释器在官方备份恢复前还原 SDK 函数和类方法的原始绑定，并移除类上
新增的补丁标记，防止游戏修改的模块别名或文字方法泄漏到下一会话；
此清理与游戏名无关。

许可与分发
----------

原 Ren'Py 及组件继续使用原许可；适配代码及共享 ABI 的 MIT 许可见
``shinki/LICENSE``。安装目录附官方完整 ``LICENSE.txt``、pthread 说明和来源
清单；SDK 的 LGPL 组件仍有对应源码与重新链接义务。
不打包 Steam、Cubism、Bink、nvdrs 或微软 D3DCompiler 再发行 DLL。
ANGLE 如需 D3DCompiler，使用 Windows 系统组件。商业资源、存档与验证画面
不属于核心包或源码仓库。

Apple SDK 输入
--------------

``shinki/sdk.json`` 记录官方 SDK ZIP（SHA256
``ff57648f9c04f27e381c48af6d8e3ee3cdec296bed4d3831f47f09b0a71b505e``）中
``lib/py3-mac-universal/librenpython.dylib`` 的实际 SHA256
``6a675b0605b1a7c82d24ca448fbaa76c1d17e2c18597c232469a534dd34b0dc3``。
该记录用于核对准备好的输入；macOS 的单架构裁剪、运行库打包和 iOS 静态运行时仍需各自的构建验收。
