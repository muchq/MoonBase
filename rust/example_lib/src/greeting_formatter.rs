pub fn format_greeting(name: &str) -> String {
    format!("Sup, {}?", name)
}

#[cfg(test)]
#[test]
fn test_format_greeting() {
    let sup = format_greeting("world");
    assert_eq!("Sup, world?", sup);
}
