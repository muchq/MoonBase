use mongodb::bson::oid::ObjectId;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Serialize, Deserialize, Debug)]
pub struct MongoDocEgg {
    pub bytes: String,
    pub version: String,
    pub metadata: HashMap<String, String>,
}

#[derive(Serialize, Deserialize)]
pub struct MongoDoc {
    pub _id: ObjectId,
    pub bytes: String,
    pub version: String,
    pub metadata: HashMap<String, String>,
}
