use chengyin_core::{demo_dictionary, Dictionary, Key, Modifiers, Session, MAX_DICTIONARY_BYTES};
use std::io::{self, BufRead, Read, Write};
use std::sync::Arc;

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let mut compile_args = std::env::args().skip(1);
    if matches!(
        compile_args.next().as_deref(),
        Some("--compile" | "--import")
    ) {
        let source = compile_args.next().ok_or("--compile 需要输入 TSV")?;
        let destination = compile_args.next().ok_or("--compile 需要输出二进制路径")?;
        if compile_args.next().is_some() {
            return Err("--compile 参数过多".into());
        }
        let mut source_text = Vec::new();
        std::fs::File::open(source)?
            .take(MAX_DICTIONARY_BYTES as u64 + 1)
            .read_to_end(&mut source_text)?;
        let dictionary = Dictionary::import(&source_text)?;
        std::fs::write(destination, dictionary.to_binary())?;
        println!(
            "已编译 {} 条，运行时堆容量 {} 字节",
            dictionary.entry_count(),
            dictionary.estimated_heap_bytes()
        );
        return Ok(());
    }
    let mut args = std::env::args().skip(1);
    let mut dictionary = demo_dictionary();
    let mut query = None;
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--dict" => {
                let path = args.next().ok_or("--dict 需要 TSV 文件路径")?;
                let mut source = Vec::new();
                std::fs::File::open(path)?
                    .take(MAX_DICTIONARY_BYTES as u64 + 1)
                    .read_to_end(&mut source)?;
                dictionary = Arc::new(Dictionary::import(&source)?);
            }
            "--query" => query = Some(args.next().ok_or("--query 需要拼音")?),
            "-h" | "--help" => {
                println!("澄音输入法 / Chengyin IME 全拼 / 首拼\n  chengyin [--dict 词典文件] [--query nihao]\n--import 词库.scel 输出.mswydict：离线转换。\n不带 --query 进入逐行演示。/quit 退出；输入 nihao 后回车展示候选。");
                return Ok(());
            }
            _ => return Err(format!("未知参数：{arg}").into()),
        }
    }
    if let Some(query) = query {
        print_query(&dictionary, &query)?;
        return Ok(());
    }
    println!(
        "澄音输入法 / Chengyin IME 全拼演示（{} 个演示词条）。/quit 退出。",
        dictionary.entry_count()
    );
    let stdin = io::stdin();
    let mut lines = stdin.lock().lines();
    loop {
        print!("拼音> ");
        io::stdout().flush()?;
        let Some(line) = lines.next() else {
            break;
        };
        let line = line?;
        if line == "/quit" {
            break;
        }
        if let Err(error) = print_query(&dictionary, &line) {
            eprintln!("{error}");
        }
    }
    Ok(())
}

fn print_query(
    dictionary: &Arc<Dictionary>,
    query: &str,
) -> Result<(), Box<dyn std::error::Error>> {
    let candidates = dictionary
        .lookup(query)
        .map_err(|error| format!("无效输入或歧义超限：{error:?}"))?;
    let _ = candidates; // validate before replay
    let mut session = Session::new(Arc::clone(dictionary));
    for c in query.chars() {
        let result = session.process(Key::Character(c), Modifiers::default());
        if result.limited {
            return Err("输入达到长度或歧义预算，未上屏".into());
        }
    }
    for index in 0..session.candidate_count() {
        let candidate = session.candidate(index).unwrap();
        println!("{}. {} [{}]", index + 1, candidate.text, candidate.pinyin);
    }
    session.process(Key::Space, Modifiers::default());
    println!("空格上屏：{}", session.commit());
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
