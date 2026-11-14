// Copyright 2026 Ye Zicheng
// SPDX-License-Identifier: Apache-2.0

fn main() {
    println!("linkspan-guest-ok platform=linux format=elf");
    let args: Vec<String> = std::env::args().skip(1).collect();
    if !args.is_empty() {
        println!("args={}", args.join("\t"));
    }
}
