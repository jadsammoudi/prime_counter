use std::env;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::process;
use std::thread;
use std::time::Instant;

/// Trial division up to sqrt(n).
fn is_prime(n: u64) -> bool {
    if n < 2 {
        return false;
    }
    if n < 4 {
        return true;
    }
    if n % 2 == 0 || n % 3 == 0 {
        return false;
    }
    let mut i: u64 = 5;
    while i * i <= n {
        if n % i == 0 || n % (i + 2) == 0 {
            return false;
        }
        i += 6;
    }
    true
}

/// Reads "numbers.txt": first line N, then N positive integers (one per line).
fn read_numbers(path: &str) -> Result<Vec<u64>, String> {
    let file = File::open(path).map_err(|e| format!("cannot open '{}': {}", path, e))?;
    let reader = BufReader::new(file);
    let mut lines = reader.lines();

    let first = lines
        .next()
        .ok_or_else(|| format!("'{}' is empty (expected N on line 1)", path))?
        .map_err(|e| format!("error reading line 1: {}", e))?;
    let n: usize = first
        .trim()
        .parse()
        .map_err(|e| format!("invalid N on line 1 ('{}'): {}", first.trim(), e))?;

    let mut numbers = Vec::with_capacity(n);
    for (idx, line) in lines.enumerate() {
        let line_no = idx + 2;
        let line = line.map_err(|e| format!("error reading line {}: {}", line_no, e))?;
        let s = line.trim();
        if s.is_empty() {
            continue; // tolerate blank lines (e.g. trailing newline)
        }
        if numbers.len() == n {
            return Err(format!("file has more than N = {} numbers (extra at line {})", n, line_no));
        }
        let v: u64 = s
            .parse()
            .map_err(|e| format!("invalid number on line {} ('{}'): {}", line_no, s, e))?;
        numbers.push(v);
    }

    if numbers.len() != n {
        return Err(format!(
            "expected {} numbers but found {}",
            n,
            numbers.len()
        ));
    }
    Ok(numbers)
}

/// Splits `numbers` into T static chunks; the last thread takes the remainder.
fn count_primes_parallel(numbers: &[u64], t: usize) -> Result<usize, String> {
    let n = numbers.len();
    let c = n / t;

    thread::scope(|s| {
        let mut handles = Vec::with_capacity(t);
        for i in 0..t {
            let start = i * c;
            let end = if i == t - 1 { n } else { (i + 1) * c };
            let slice = &numbers[start..end];
            let handle = thread::Builder::new()
                .name(format!("worker-{}", i))
                .spawn_scoped(s, move || slice.iter().filter(|&&x| is_prime(x)).count())
                .map_err(|e| format!("failed to spawn thread {}: {}", i, e))?;
            handles.push(handle);
        }

        let mut total = 0;
        for (i, h) in handles.into_iter().enumerate() {
            total += h
                .join()
                .map_err(|_| format!("thread {} panicked", i))?;
        }
        Ok(total)
    })
}

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        return Err(format!("usage: {} <num_threads>", args[0]));
    }
    let t: usize = args[1]
        .parse()
        .map_err(|_| format!("invalid thread count '{}': must be a positive integer", args[1]))?;
    if t < 1 {
        return Err("thread count must be >= 1".to_string());
    }

    let numbers = read_numbers("numbers.txt")?;
    if numbers.is_empty() {
        println!("Total prime found: 0");
        return Ok(());
    }
    // More threads than numbers would leave chunks of size 0; cap it.
    let t = t.min(numbers.len());

    let start = Instant::now();
    let total = count_primes_parallel(&numbers, t)?;
    let elapsed = start.elapsed();

    println!("Total prime found: {}", total);
    println!("➢ Time taken: {:.3} seconds", elapsed.as_secs_f64());
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        eprintln!("Error: {}", e);
        process::exit(1);
    }
}
