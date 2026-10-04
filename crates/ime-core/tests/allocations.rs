use myswy_core::{demo_dictionary, Dictionary, Key, Modifiers, Profile, Session};
use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

struct CountingAllocator;
static COUNTING: AtomicBool = AtomicBool::new(false);
static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);
unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if COUNTING.load(Ordering::Relaxed) {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        }
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        if COUNTING.load(Ordering::Relaxed) {
            ALLOCATIONS.fetch_add(1, Ordering::Relaxed);
        }
        unsafe { System.realloc(ptr, layout, size) }
    }
}
#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

// Isolated integration-test process, one test: other test threads cannot skew counts.
#[test]
fn initialized_key_path_does_not_allocate() {
    let mut s = Session::new(demo_dictionary());
    let daily = std::sync::Arc::new(
        Dictionary::from_binary(include_bytes!("../../../data/daily.mswydict")).unwrap(),
    );
    let mut real = Session::new(daily);
    assert!(std::mem::size_of::<Session>() + real.estimated_heap_bytes() <= 64 * 1024);
    let mut profile = Profile::default();
    assert!(profile.record("shi", "士"));
    assert!(profile.record("wxhzw", "我喜欢中文"));
    assert!(real.set_profile(std::sync::Arc::new(profile)));
    let mut display = [0u8; 319];
    COUNTING.store(true, Ordering::SeqCst);
    for _ in 0..100 {
        for c in "zhong'guoren".chars() {
            s.process(Key::Character(c), Modifiers::default());
        }
        s.process(Key::Backspace, Modifiers::default());
        s.process(Key::Down, Modifiers::default());
        s.process(Key::Space, Modifiers::default());
        s.reset();
        for c in "woxihuanzhongwen".chars() {
            real.process(Key::Character(c), Modifiers::default());
            std::hint::black_box(real.display_preedit(&mut display));
        }
        real.process(Key::Home, Modifiers::default());
        real.process(Key::Right, Modifiers::default());
        real.process(Key::Delete, Modifiers::default());
        real.process(Key::Character('o'), Modifiers::default());
        real.process(Key::Space, Modifiers::default());
        real.reset();
        for c in "shi".chars() {
            real.process(Key::Character(c), Modifiers::default());
            std::hint::black_box(real.display_preedit(&mut display));
        }
        for _ in 0..20 {
            real.process(Key::PageDown, Modifiers::default());
        }
        for _ in 0..20 {
            real.process(Key::PageUp, Modifiers::default());
        }
        real.reset();
        for c in "wxhzw".chars() {
            real.process(Key::Character(c), Modifiers::default());
            std::hint::black_box(real.display_preedit(&mut display));
        }
        real.process(Key::Space, Modifiers::default());
        real.process(Key::Tab, Modifiers::default());
        real.reset();
        for c in "nihao".chars() {
            real.process(Key::Character(c), Modifiers::default());
            std::hint::black_box(real.display_preedit(&mut display));
        }
        real.process(Key::Space, Modifiers::default());
        real.process(Key::PageDown, Modifiers::default());
        real.process(Key::PageUp, Modifiers::default());
        real.process(Key::Select(0), Modifiers::default());
        real.reset();
    }
    COUNTING.store(false, Ordering::SeqCst);
    assert_eq!(ALLOCATIONS.load(Ordering::SeqCst), 0);
}
