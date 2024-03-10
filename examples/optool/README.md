# optool

Source: [https://github.com/alexzielenski/optool](https://github.com/alexzielenski/optool)

`optool`: The original binary without obfuscation

`optool_obfuscated`: The obfuscated binary

`optool_obfuscated_stripped`: The obfuscated binary, stripped with [machostrip](https://github.com/61bcdefg/machostrip)

Branch of toolchain: `llvm-swift-5.10-RELEASE`

Optimization Level:  `-Os`

Obfuscation flags:
```
-mllvm
-enable-bcfobf
-mllvm
-bcf_onlyjunkasm
-mllvm
-bcf_prob=100
-mllvm
-enable-cffobf
-mllvm
-enable-splitobf
-mllvm
-enable-strcry
-mllvm
-enable-indibran
-mllvm
-indibran-enc-jump-target
-mllvm
-enable-fco
```
