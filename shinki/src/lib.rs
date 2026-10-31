//! Ren'Py's original VM, resident CPython interpreter and GL2 renderer run in
//! the linked native core. The host communicates through the shared C ABI.
use shinki_emu_abi::{CoreApi, Query, Status};

unsafe extern "C" {
    #[cfg_attr(feature = "static", link_name = "shinki_emu_renpy_query")]
    #[cfg_attr(not(feature = "static"), link_name = "shinki_emu_query")]
    pub fn query(version: u32, output: *mut *const CoreApi) -> Status;
}

pub const QUERY: Query = query;

#[cfg(feature = "static")]
unsafe extern "C" {
    fn shinki_emu_renpy_set_resource_root(
        path: shinki_emu_abi::Bytes,
        error: *mut shinki_emu_abi::Error,
    ) -> Status;
}

/// Configure packaged runtime assets before the registry creates a module owner.
#[cfg(feature = "static")]
pub fn set_resource_root(path: &str, error: &mut shinki_emu_abi::Error) -> Status {
    unsafe {
        shinki_emu_renpy_set_resource_root(
            shinki_emu_abi::Bytes {
                data: path.as_ptr(),
                len: path.len(),
            },
            error,
        )
    }
}
