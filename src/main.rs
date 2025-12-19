use anyhow::Ok;
use anyhow::Result;
#[allow(unused_imports)]
use std::io;
use std::net;

fn main() -> Result<()> {
    let _listener = net::TcpListener::bind("127.0.0.1")?;
    Ok(())
}
