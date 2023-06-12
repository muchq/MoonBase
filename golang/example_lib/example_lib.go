package main

import "C"

//export Greet
func Greet(cname *C.char) *C.char {
	name := C.GoString(cname)
	return C.CString("Sup " + name)
}

func main() {
}
