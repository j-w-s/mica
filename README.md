this is still a work in progress, so updates/documentation will be delayed (though they will eventually follow).

i had initially attempted to create something similar to this but full-fledged with its compiler written in rust, but i realized i
was slightly in over my head and opted for something scoped down, more simialr to lua.

more tests for the language to be written. current effort is being put toward fixing complex scoping and memory management issues w/ regard to
how the interpreter handles variable resolution in nested blocks and closures

# small, embeddable, and fast
# register-based virtual machine
# functional by design with closure support (upvalues)
# refcount gc, so memory is deterministic (also makes my life harder)
# c api for host integration
# immutable by default

### embed in c
```c
#include "mica.h"

int main() {
    VM* vm = mica_new();
    
    mica_compile(vm, "let x = 10; print(x)");
    mica_run(vm);
    
    mica_free(vm);
    return 0;
}
```

## language guide

### variables
```mica
let x = 10              // immutable
let mut y = 20          // mutable
y = y + 1
```

### functions
```mica
fn add(a, b) {
  return a + b
}

let result = add(5, 10)
```

### closures
```mica
fn make_counter(start) {
  let mut count = start
  return || {
    count = count + 1
    return count
  }
}

let counter = make_counter(0)
counter()  // 1
counter()  // 2
```

### arrays
```mica
let nums = [1, 2, 3, 4, 5]
print(nums[0])
nums[1] = 10

for x in nums {
  print(x)
}
```

### control flow
```mica
if x > 10 {
  print("big")
} else {
  print("small")
}

while x > 0 {
  x = x - 1
}

loop {
  if done { break }
}
```

### zero-cost abstractions via iterators (partial)
```mica
let result = nums
  .iter()
  .filter(|x| x > 2)
  .map(|x| x * 2)
  .fold(0, |acc, x| acc + x)
```
