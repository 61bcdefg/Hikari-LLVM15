namespace n {
    struct D {
        int i;
        static int anInt() { return 2; }
        int dump() { return i; }
    };
}

using namespace n;

int foo(D* D) {
    // SWIFT-LLDB CHANGE: Contrary to llvm.org this setting defaults to false.
    return D->dump(); //% self.expect("settings set target.experimental.inject-local-vars true")
    //% self.expect("expression -- D->dump()", DATA_TYPES_DISPLAYED_CORRECTLY, substrs = ["int", "2"])
    //% self.expect("settings set target.experimental.inject-local-vars false")
}

int main (int argc, char const *argv[])
{
    D myD { D::anInt() };
    foo(&myD);
    return 0; 
}
