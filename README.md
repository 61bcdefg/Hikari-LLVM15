# Hikari-LLVM15
 A fork of HikariObfuscator [WIP]
 
 ## 原项目链接
 [https://github.com/HikariObfuscator/Hikari](https://github.com/HikariObfuscator/Hikari)

## 使用

下载后编译

### Swift混淆支持

编译[Swift Toolchain](https://github.com/NeHyci/Hikari-Swift)的时间非常长，还可以使用[Hanabi](https://github.com/NeHyci/Hanabi)。但是如果没有一些特殊需求，最好使用工具链形式

需要注意的是添加混淆参数的位置是在**Swift Compiler - Other Flags**中的**Other Swift Flags**，并且是在前面加-Xllvm，而不是-mllvm。
关闭优化的地方在**Swift Compiler - Code Generation**中的**Optimization Level**，设置为 *No Optimization [-Onone]*

每次修改Other Swift Flags后编译前需要先Shift+Command+K(Clean Build Folder)，因为Swift并不会像OC一样检测到项目cflag的修改就会重新编译

### PreCompiled IR

PreCompiled IR是指自定义的LLVM Bitcode文件，可以通过在存在回调函数的源文件的编译命令(C Flags)中加上`-emit-llvm`生成，然后放到指定位置即可

###  混淆选项

这里只会介绍修改的部分，原项目存在的功能请自行前往[https://github.com/HikariObfuscator/Hikari/wiki/](https://github.com/HikariObfuscator/Hikari/wiki/)查看

#### AntiClassDump

-acd-rename-methodimp

重命名在IDA中显示的方法函数名称(修改为ACDMethodIMP)，不是修改方法名。默认关闭

#### AntiHooking

整体开启这个功能会使生成的二进制文件大小急剧膨胀，建议只在部分函数开启这个功能(toObfuscate)

支持检测Objective-C运行时Hook。如果检测到就会调用AHCallBack函数(从PreCompiled IR获取)，如果不存在AHCallBack，就会退出程序。

InlineHook检测目前只支持arm64，在函数中插入代码检测当前函数是否被Hook，如果检测到就会调用AHCallBack函数(从PreCompiled IR获取)，如果不存在AHCallBack，就会退出程序。

-enable-antihook

启用AntiHooking。默认关闭

-ah_inline

检测当前函数是否被inline hook。默认开启

-ah_objcruntime

检测当前函数是否被runtime hook。默认开启

-ah_antirebind

使生成的文件无法被fishhook重绑定符号。默认关闭

-adhexrirpath

AntiHooking PreCompiled IR文件的路径

#### AntiDebugging

自动在函数中进行反调试，如果有InitADB和ADBCallBack函数(从PreCompiled IR获取)，就会调用ADBInit函数，如果不存在InitADB和ADBCallBack函数并且是Apple ARM64平台，就会自动在void返回类型的函数中插入内联汇编反调试，否则不做处理。

-enable-adb

启用AntiDebugging。默认关闭

-adb_prob

每个函数被添加反调试的概率。默认为40

-adbextirpath

AntiDebugging PreCompiled IR文件的路径

#### StringEncryption

-strcry_prob

每个字符串中每个byte被加密的概率。默认为100。这个功能是为了给一些需要的加密强度不高，但是重视体积的人。

#### BogusControlFlow

-bcf_onlyjunkasm

在虚假块中只插入花指令

-bcf_junkasm

在虚假块中插入花指令，干扰IDA对函数的识别。默认关闭

-bcf_junkasm_minnum

在虚假块中花指令的最小数量。默认为2

-bcf_junkasm_maxnum

在虚假块中花指令的最大数量。默认为4

-bcf_createfunc

使用函数封装不透明谓词。默认关闭

#### ConstantEncryption

修改自https://iosre.com/t/llvm-llvm/11132

-enable-constenc

启用ConstantEncryption。默认关闭

-constenc_times

ConstantEncryption在每个函数混淆的次数。默认为1

-constenc_prob

每个指令被ConstantEncryption混淆的概率。默认为50

-constenc_togv

将数字常量替换为全局变量，以对抗反编译器自动简化表达式。默认关闭

-constenc_subxor

替换ConstantEncryption的XOR运算，使其变得更加复杂

#### IndirectBranch

-indibran-use-stack

将跳转表的地址加载到栈中，再从栈中读取。默认关闭

-indibran-enc-jump-target

加密跳转表和索引。默认关闭

### Functions Annotations

#### New Supported Flags

-   `adb` Anti Debugging
-   `antihook` Anti Hooking
-   `constenc` Constant Encryption
