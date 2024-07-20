use crate::model::{MongoDoc, MongoDocEgg};
use escapist_proto::escapist::escapist_server::Escapist;
use escapist_proto::escapist::{
    Document, FindDocByIdRequest, FindDocByIdResponse, FindDocRequest, FindDocResponse,
    InsertDocRequest, InsertDocResponse, UpdateDocRequest, UpdateDocResponse,
};
use mongodb::bson::oid::ObjectId;
use mongodb::bson::Document as BsonDocument;
use mongodb::bson::{doc, Bson};
use mongodb::{Client, Collection};
use std::collections::HashMap;
use tonic::{Request, Response, Status};
use uuid::Uuid;

const DB_NAME: &str = "ESCAPIST";

#[derive(Debug)]
pub struct EscapistService {
    pub client: Client,
}

fn get_collection<T: Send + Sync>(client: &Client, collection_name: String) -> Collection<T> {
    client
        .database(DB_NAME)
        .collection(collection_name.as_str())
}

fn convert_hashmap_to_document(hash_map: HashMap<String, String>) -> BsonDocument {
    let metadata_doc: BsonDocument = hash_map.into_iter()
        .map(|(k, v)| (k, Bson::String(v)))
        .collect();
    let mut query_doc = BsonDocument::new();
    query_doc.insert("metadata", metadata_doc);
    query_doc
}

#[tonic::async_trait]
impl Escapist for EscapistService {
    async fn insert_doc(
        &self,
        request: Request<InsertDocRequest>,
    ) -> Result<Response<InsertDocResponse>, Status> {
        let req = request.into_inner();
        if req.doc.is_none() {
            return Err(Status::invalid_argument("document is required"));
        }

        let version_string = Uuid::new_v4().to_string();
        let doc_egg = req.doc.unwrap();
        let mongo_doc = MongoDocEgg {
            bytes: doc_egg.bytes,
            version: version_string.clone(),
            metadata: doc_egg.metadata,
        };
        let col: Collection<MongoDocEgg> = get_collection(&self.client, req.collection);

        match col.insert_one(mongo_doc).await {
            Ok(res) => Ok(Response::new(InsertDocResponse {
                id: res.inserted_id.to_string(),
                version: version_string,
            })),
            // this may not be an internal error...
            Err(_) => Err(Status::internal("internal error")),
        }
    }

    async fn update_doc(
        &self,
        request: Request<UpdateDocRequest>,
    ) -> Result<Response<UpdateDocResponse>, Status> {
        let req = request.into_inner();
        if req.doc.is_none() {
            return Err(Status::invalid_argument("document is required"));
        }
        let doc_to_update = req.doc.unwrap();
        let id_result = ObjectId::parse_str(doc_to_update.id);
        if id_result.is_err() {
            return Err(Status::not_found("not found"))
        }
        let id = id_result.unwrap();
        let expected_version = doc_to_update.version;

        let col: Collection<MongoDoc> = get_collection(&self.client, req.collection);

        let new_version = Uuid::new_v4().to_string();
        let replacement_doc = MongoDoc {
            _id: id,
            version: new_version.clone(),
            bytes: doc_to_update.bytes,
            metadata: doc_to_update.metadata,
        };

        let query = doc! { "_id": id, "version": expected_version };

        match col.find_one_and_replace(query, replacement_doc).await {
            Ok(Some(_)) => Ok(Response::new(UpdateDocResponse {
                id: id.to_hex(),
                version: new_version,
            })),
            Ok(None) => Err(Status::not_found("unknown document")),
            Err(_) => Err(Status::internal("internal error")),
        }
    }

    async fn find_doc_by_id(
        &self,
        request: Request<FindDocByIdRequest>,
    ) -> Result<Response<FindDocByIdResponse>, Status> {
        let req = request.into_inner();
        let col: Collection<MongoDoc> = get_collection(&self.client, req.collection);
        let id_result = ObjectId::parse_str(&req.id);
        if id_result.is_err() {
            return Err(Status::not_found("not found"))
        }

        let id = id_result.unwrap();

        match col.find_one(doc! { "_id": id }).await {
            Ok(Some(mongo_doc)) => {
                let res = FindDocByIdResponse {
                    doc: Some(Document {
                        id: mongo_doc._id.to_string(),
                        version: mongo_doc.version,
                        bytes: mongo_doc.bytes,
                        metadata: HashMap::new(),
                    }),
                };
                Ok(Response::new(res))
            }
            Ok(None) => Err(Status::not_found("not found")),
            Err(_) => Err(Status::internal("internal error")),
        }
    }

    async fn find_doc(
        &self,
        request: Request<FindDocRequest>,
    ) -> Result<Response<FindDocResponse>, Status> {
        let req = request.into_inner();
        if req.query.is_empty() {
            return Err(Status::invalid_argument("query constraints are required"));
        }
        let bson_query = convert_hashmap_to_document(req.query);

        let col: Collection<MongoDoc> = get_collection(&self.client, req.collection);

        match col.find_one(bson_query).await {
            Ok(Some(found)) => Ok(Response::new(FindDocResponse {
                doc: Some(Document {
                    id: found._id.to_hex(),
                    version: found.version,
                    bytes: found.bytes,
                    metadata: found.metadata,
                }),
            })),
            Ok(None) => Err(Status::not_found("not found")),
            Err(_) => Err(Status::internal("internal error")),
        }
    }
}
