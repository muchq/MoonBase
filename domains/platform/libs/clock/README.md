# Clock

Use the clock interface to access `time.Time` so that modules stay testable.

### Examples
prod:
```go
type Foo struct {
    Clock Clock
}

func (f *Foo) SecondsSinceEpochIsEven() bool {
    return f.Clock().Unix() % 2 == 0
}

func main() {
    foo := &Foo{
        Clock: NewSystemUtcClock(),
    }

    fmt.PrintLn(foo.SecondsSinceEpochIsEven())
}
```

test:
```go
func TestFoo_SecondsSinceEpochIsEven(t * testing.T) {
    testClock := NewTestClock()
    foo := &Foo{
        Clock: testClock,
    }

    assert.True(t, foo.SecondsSinceEpochIsEven(), "zero is even")

    testClock.Tick(1)
    assert.False(t, foo.SecondsSinceEpochIsEven(), "1 is odd")
}
```
