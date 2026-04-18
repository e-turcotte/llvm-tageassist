; RUN: opt -passes="nop-inject" -S %s | FileCheck %s

define i32 @foo(i32 %a, i32 %b) {
entry:
  %cmp = icmp sgt i32 %a, %b
  br i1 %cmp, label %yes, label %no

; CHECK: call void asm sideeffect "nop"
; CHECK-NEXT: br i1

yes:
  ret i32 %a
no:
  ret i32 %b
}
