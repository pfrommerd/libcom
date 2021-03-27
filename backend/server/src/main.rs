use server::Server;

use std::env;

use log::{error, info};
use tokio::net::TcpListener;

use anyhow::{anyhow, Result};

#[tokio::main]
async fn main() -> Result<()> {
    env_logger::init();

    //Build Rust code from .proto files
    //prost_build::compile_protos(&["../../api.proto", "../../common.proto", "../../log.proto", "../../stream.proto"], &["../../"]).unwrap();
    // On further note this appears to be done already in build.rs




    let addr = match env::var("TELEGRAPH_SERVER_ADDR") {
        Ok(addr) => addr,
        Err(env::VarError::NotPresent) => "0.0.0.0:8081".to_string(),
        Err(err) => return Err(anyhow!("error reading the server address: {}", err)),
    };

    let listener = TcpListener::bind(&addr).await?;

    info!("Listening on: {}", addr);

    loop {
        let (stream, _) = listener.accept().await?;

        tokio::spawn(async move {
            let server = Server::new();

            if let Err(error) = server.accept_connection(stream).await {
                error!("Closing connection due to error: {}", error)
            }
        });
    } 
}
