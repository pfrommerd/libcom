use anyhow::{anyhow, Context, Result};
use futures_util::StreamExt;
use prost::Message as _;
use std::io::Cursor;
use tokio::net::TcpStream; // Import the message trait so that we can decode `Packet`s

pub mod common;
pub mod errors;
pub mod local;
pub mod nodes;
pub mod types;
pub mod value;
// Disable clippy warnings for generated code, since we can't control/fix them
#[allow(clippy::all)]
pub mod wire;

pub mod items {
    include!(concat!(env!("OUT_DIR"), "/telegraph.items.rs"));
}

pub fn create_val() -> wire::log::CallFinished {
    let mut output = wire::log::CallFinished::default();
    output
}

pub fn create_car() -> items::Car {
    let mut car = items::Car::default();
    car.color = "blue".to_string();
    car.set_model(items::car::Model::Rev6);
    car
}

pub fn serialize<T: prost::Message>(arg: T) -> Vec<u8> {
    let mut buf = Vec::new();
    buf.reserve(arg.encoded_len());
    arg.encode(&mut buf).unwrap();
    buf
}

// pub fn deserialize(buf: &[u8]) -> Result<dyn: dyn prost::Message, prost::DecodeError>
// {
//     dyn prost::Message::decode(&mut Cursor::new(buf));
// }

pub fn serialize_car(car: &items::Car) -> Vec<u8> {
    let mut buf = Vec::new();
    buf.reserve(car.encoded_len());
    car.encode(&mut buf).unwrap();
    buf
}

pub fn deserialize_car(buf: &[u8]) -> Result<items::Car, prost::DecodeError> {
    items::Car::decode(&mut Cursor::new(buf))
}


pub struct Server {}

impl Server {
    // TODO: I think that this needs to take a namespace
    pub fn new() -> Server {
        Server {}
    }

    pub async fn accept_connection(self, stream: TcpStream) -> Result<()> {
        let (mut _write, mut read) = tokio_tungstenite::accept_async(stream)
            .await
            .context("failed to accept the connection")?
            .split();

        while let Some(msg) = read.next().await {
            match msg {
                Ok(tungstenite::Message::Binary(bytes)) => {
                    let packet =
                        wire::api::Packet::decode(bytes.as_slice()).context("decoding message")?;

                    // Handle the packet
                    println!("{:?}", packet);
                }
                Ok(message) => {
                    return Err(anyhow!("got unexpected message: {:?}", message));
                }
                Err(error) => {
                    return Err(anyhow!("error while reading a message: {}", error));
                }
            }
        }

        Ok(())
    }
}

impl Default for Server {
    fn default() -> Self {
        Self::new()
    }
}
