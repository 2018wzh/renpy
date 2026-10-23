//! Ren'Py's original VM, resident CPython interpreter and GL2 renderer run in
//! the linked native core. The host communicates through the shared C ABI.
use shinki_emu_abi::{CoreApi, Query, Status};

unsafe extern "C" {
    #[link_name = "shinki_emu_query"]
    pub fn query(version: u32, output: *mut *const CoreApi) -> Status;
}

pub const QUERY: Query = query;
