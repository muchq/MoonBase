use mongodb::bson::oid::ObjectId;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Serialize, Deserialize, Debug)]
pub struct MongoDocEgg {
    pub bytes: Vec<u8>,
    pub version: String,
    pub tags: HashMap<String, String>,
}

#[derive(Serialize, Deserialize)]
pub struct MongoDoc {
    pub _id: ObjectId,
    pub bytes: Vec<u8>,
    pub version: String,
    pub tags: HashMap<String, String>,
}
