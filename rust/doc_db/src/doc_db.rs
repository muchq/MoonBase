use crate::model::{MongoDoc, MongoDocEgg};
use doc_db_proto::doc_db::doc_db_server::DocDb;
use doc_db_proto::doc_db::{
    Document, FindDocByIdRequest, FindDocByIdResponse, FindDocRequest, FindDocResponse,
    InsertDocRequest, InsertDocResponse, UpdateDocRequest, UpdateDocResponse,
};
use mongodb::bson::oid::ObjectId;
use mongodb::bson::Document as BsonDocument;
use mongodb::bson::{doc, Bson};
use mongodb::error::{Error as MongoError, Result as MongoResult};
#[cfg(not(test))]
use mongodb::{Client, Collection};
use std::collections::HashMap;
use tonic::metadata::MetadataMap;
use tonic::{Request, Response, Status};
#[cfg(not(test))]
use uuid::Uuid;

trait Crud {
    async fn insert_one(
        &self,
        db_name: String,
        collection: String,
        doc_egg: MongoDocEgg,
    ) -> Result<ObjectId, MongoError>;

    async fn update_one(
        &self,
        db_name: String,
        collection: String,
        query: BsonDocument,
        replacement: MongoDoc,
    ) -> MongoResult<Option<MongoDoc>>;

    async fn find_one(
        &self,
        db_name: String,
        collection: String,
        query: BsonDocument,
    ) -> MongoResult<Option<MongoDoc>>;

    fn new_uuid(&self) -> String;
}

fn validate_insert_request(req: &InsertDocRequest) -> Result<(), Status> {
    if req.collection.is_empty() {
        return Err(Status::invalid_argument("collection is required"));
    }
    if req.doc.is_none() {
        return Err(Status::invalid_argument("document is required"));
    }
    Ok(())
}

fn validate_update_request(req: &UpdateDocRequest) -> Result<(), Status> {
    if req.collection.is_empty() {
        return Err(Status::invalid_argument("collection is required"));
    }
    if req.doc.is_none() {
        return Err(Status::invalid_argument("document is required"));
    }
    if req.id.is_empty() {
        return Err(Status::invalid_argument("id is required"));
    }
    if req.version.is_empty() {
        return Err(Status::invalid_argument("version is required"));
    }
    Ok(())
}

fn validate_find_by_id_request(req: &FindDocByIdRequest) -> Result<(), Status> {
    if req.collection.is_empty() {
        return Err(Status::invalid_argument("collection is required"));
    }
    if req.id.is_empty() {
        return Err(Status::invalid_argument("id is required"));
    }
    Ok(())
}

fn validate_find_by_tags_request(req: &FindDocRequest) -> Result<(), Status> {
    if req.collection.is_empty() {
        return Err(Status::invalid_argument("collection is required"));
    }
    if req.tags.is_empty() {
        return Err(Status::invalid_argument("tags are required"));
    }
    Ok(())
}

#[cfg(not(test))]
type MongoClient = Client;

#[cfg(test)]
type MongoClient = u8;

#[derive(Debug)]
pub struct DocDbService {
    pub client: MongoClient,
}

fn convert_hashmap_to_document(hash_map: HashMap<String, String>) -> BsonDocument {
    let tags_doc: BsonDocument = hash_map
        .into_iter()
        .map(|(k, v)| (k, Bson::String(v)))
        .collect();
    let mut query_doc = BsonDocument::new();
    query_doc.insert("tags", tags_doc);
    query_doc
}

const DB_NAME_KEY: &str = "db_namespace";

fn read_db_name_from_metadata(metadata: &MetadataMap) -> Option<String> {
    let db_name_maybe = metadata.get(DB_NAME_KEY);
    db_name_maybe?;
    let db_name = db_name_maybe.unwrap();
    let db_str_maybe = db_name.to_str();
    if db_str_maybe.is_err() {
        return None;
    }
    let db_str = db_str_maybe.unwrap();
    let db_string = db_str.to_string();
    if db_string.is_empty() {
        return None;
    }
    Some(db_string)
}

#[tonic::async_trait]
impl DocDb for DocDbService {
    async fn insert_doc(
        &self,
        request: Request<InsertDocRequest>,
    ) -> Result<Response<InsertDocResponse>, Status> {
        let db_name_maybe = read_db_name_from_metadata(request.metadata());
        if db_name_maybe.is_none() {
            return Err(Status::invalid_argument("db_namespace is required"));
        }
        let db_name = db_name_maybe.unwrap();
        let req = request.into_inner();
        validate_insert_request(&req)?;

        let version_string = self.new_uuid();
        let doc_egg = req.doc.unwrap();
        let mongo_doc = MongoDocEgg {
            bytes: doc_egg.bytes,
            version: version_string.clone(),
            tags: doc_egg.tags,
        };

        match self.insert_one(db_name, req.collection, mongo_doc).await {
            Ok(object_id) => Ok(Response::new(InsertDocResponse {
                id: object_id.to_hex(),
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
        let db_name_maybe = read_db_name_from_metadata(request.metadata());
        if db_name_maybe.is_none() {
            return Err(Status::invalid_argument("db_namespace is required"));
        }
        let db_name = db_name_maybe.unwrap();
        let req = request.into_inner();
        validate_update_request(&req)?;

        let doc_to_update = req.doc.unwrap();
        let id_result = ObjectId::parse_str(req.id);
        if id_result.is_err() {
            return Err(Status::not_found("not found"));
        }
        let id = id_result.unwrap();
        let expected_version = req.version;

        let new_version = self.new_uuid();
        let replacement_doc = MongoDoc {
            _id: id,
            version: new_version.clone(),
            bytes: doc_to_update.bytes,
            tags: doc_to_update.tags,
        };

        let query = doc! { "_id": id, "version": expected_version };
        match self
            .update_one(db_name, req.collection, query, replacement_doc)
            .await
        {
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
        let db_name_maybe = read_db_name_from_metadata(request.metadata());
        if db_name_maybe.is_none() {
            return Err(Status::invalid_argument("db_namespace is required"));
        }
        let db_name = db_name_maybe.unwrap();
        let req = request.into_inner();
        validate_find_by_id_request(&req)?;

        let id_result = ObjectId::parse_str(&req.id);
        if id_result.is_err() {
            return Err(Status::not_found("not found"));
        }

        let id = id_result.unwrap();
        match self
            .find_one(db_name, req.collection, doc! { "_id": id })
            .await
        {
            Ok(Some(mongo_doc)) => {
                let res = FindDocByIdResponse {
                    doc: Some(Document {
                        id: mongo_doc._id.to_string(),
                        version: mongo_doc.version,
                        bytes: mongo_doc.bytes,
                        tags: mongo_doc.tags,
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
        let db_name_maybe = read_db_name_from_metadata(request.metadata());
        if db_name_maybe.is_none() {
            return Err(Status::invalid_argument("db_namespace is required"));
        }
        let db_name = db_name_maybe.unwrap();
        let req = request.into_inner();
        validate_find_by_tags_request(&req)?;

        let bson_query = convert_hashmap_to_document(req.tags);
        match self.find_one(db_name, req.collection, bson_query).await {
            Ok(Some(found)) => Ok(Response::new(FindDocResponse {
                doc: Some(Document {
                    id: found._id.to_hex(),
                    version: found.version,
                    bytes: found.bytes,
                    tags: found.tags,
                }),
            })),
            Ok(None) => Err(Status::not_found("not found")),
            Err(_) => Err(Status::internal("internal error")),
        }
    }
}

#[cfg(not(test))]
fn get_collection<T: Send + Sync>(
    client: &Client,
    db_name: String,
    collection_name: String,
) -> Collection<T> {
    client
        .database(db_name.as_str())
        .collection(collection_name.as_str())
}

#[cfg(not(test))]
impl Crud for DocDbService {
    async fn insert_one(
        &self,
        db_name: String,
        collection: String,
        doc_egg: MongoDocEgg,
    ) -> Result<ObjectId, MongoError> {
        let collection: Collection<MongoDocEgg> = get_collection(&self.client, db_name, collection);
        let result = collection.insert_one(doc_egg).await;
        result.map(|r| r.inserted_id.as_object_id().unwrap())
    }

    async fn update_one(
        &self,
        db_name: String,
        collection: String,
        query: BsonDocument,
        replacement: MongoDoc,
    ) -> MongoResult<Option<MongoDoc>> {
        let collection: Collection<MongoDoc> = get_collection(&self.client, db_name, collection);
        collection.find_one_and_replace(query, replacement).await
    }

    async fn find_one(
        &self,
        db_name: String,
        collection: String,
        query: BsonDocument,
    ) -> MongoResult<Option<MongoDoc>> {
        let collection: Collection<MongoDoc> = get_collection(&self.client, db_name, collection);
        collection.find_one(query).await
    }

    fn new_uuid(&self) -> String {
        Uuid::new_v4().to_string()
    }
}

#[cfg(test)]
mod tests {
    use crate::doc_db::*;
    use doc_db_proto::doc_db::DocumentEgg;
    use tonic::metadata::MetadataValue;

    const TEST_ID_STRING: &str = "66a040ff87471136d177c687";
    const TEST_VERSION_STRING: &str = "02250728-a46d-4b97-ab68-41a26319b98e";
    fn to_object_id(obj_id_str: &str) -> ObjectId {
        ObjectId::parse_str(obj_id_str).unwrap()
    }

    impl Crud for DocDbService {
        async fn insert_one(
            &self,
            _db_name: String,
            _collection: String,
            _doc_egg: MongoDocEgg,
        ) -> Result<ObjectId, MongoError> {
            if cfg!(feature = "rpc_success") {
                Ok(to_object_id(TEST_ID_STRING))
            } else {
                Err(MongoError::custom("broken"))
            }
        }

        async fn update_one(
            &self,
            _db_name: String,
            _collection: String,
            _query: BsonDocument,
            replacement: MongoDoc,
        ) -> MongoResult<Option<MongoDoc>> {
            if cfg!(feature = "rpc_success") {
                Ok(Some(replacement))
            } else {
                Err(MongoError::custom("broken"))
            }
        }

        async fn find_one(
            &self,
            _db_name: String,
            _collection: String,
            _query: BsonDocument,
        ) -> MongoResult<Option<MongoDoc>> {
            if cfg!(feature = "rpc_success") {
                let mut tags = HashMap::new();
                tags.insert("player_1".to_string(), "Tippy".to_string());
                Ok(Some(MongoDoc {
                    _id: to_object_id(TEST_ID_STRING),
                    version: TEST_VERSION_STRING.to_string(),
                    bytes: "neat document bytes".as_bytes().to_vec(),
                    tags,
                }))
            } else {
                Err(MongoError::custom("broken"))
            }
        }

        fn new_uuid(&self) -> String {
            TEST_VERSION_STRING.to_string()
        }
    }

    fn present_doc_egg() -> Option<DocumentEgg> {
        Some(DocumentEgg {
            bytes: "foo".as_bytes().to_vec(),
            tags: HashMap::new(),
        })
    }

    #[cfg(feature = "rpc_success")]
    fn present_doc() -> Option<Document> {
        let mut tags = HashMap::new();
        tags.insert("player_1".to_string(), "Tippy".to_string());
        return Some(Document {
            id: TEST_ID_STRING.to_string(),
            version: TEST_VERSION_STRING.to_string(),
            bytes: "neat document bytes".as_bytes().to_vec(),
            tags,
        });
    }

    const UNIT_UNDER_TEST: DocDbService = DocDbService { client: 0 };

    #[tokio::test]
    #[cfg(feature = "rpc_success")]
    async fn insert_doc_success() {
        let mut req = Request::new(InsertDocRequest {
            collection: "foo".to_string(),
            doc: present_doc_egg(),
        });
        req.metadata_mut()
            .append("db_namespace", MetadataValue::from_static("test"));
        let response = UNIT_UNDER_TEST.insert_doc(req).await.unwrap().into_inner();
        assert_eq!(response.id, TEST_ID_STRING);
        assert_eq!(response.version, TEST_VERSION_STRING);
    }

    #[tokio::test]
    #[cfg(feature = "rpc_success")]
    async fn update_doc_success() {
        let mut req = Request::new(UpdateDocRequest {
            collection: "foo".to_string(),
            id: TEST_ID_STRING.to_string(),
            version: "123".to_string(),
            doc: present_doc_egg(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let response = UNIT_UNDER_TEST.update_doc(req).await.unwrap().into_inner();
        assert_eq!(response.id, TEST_ID_STRING);
        assert_eq!(response.version, TEST_VERSION_STRING);
    }

    #[tokio::test]
    #[cfg(feature = "rpc_success")]
    async fn find_by_id_success() {
        let mut req = Request::new(FindDocByIdRequest {
            collection: "foo".to_string(),
            id: TEST_ID_STRING.to_string(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let response = UNIT_UNDER_TEST
            .find_doc_by_id(req)
            .await
            .unwrap()
            .into_inner();
        assert_eq!(response.doc, present_doc());
    }

    #[tokio::test]
    #[cfg(feature = "rpc_success")]
    async fn find_by_tags_success() {
        let mut tags = HashMap::new();
        tags.insert("player_1".to_string(), "Tippy".to_string());
        let mut req = Request::new(FindDocRequest {
            collection: "foo".to_string(),
            tags,
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let response = UNIT_UNDER_TEST.find_doc(req).await.unwrap().into_inner();
        assert_eq!(response.doc, present_doc());
    }

    #[tokio::test]
    #[cfg(not(feature = "rpc_success"))]
    async fn insert_doc_failed() {
        let mut req = Request::new(InsertDocRequest {
            collection: "foo".to_string(),
            doc: Some(DocumentEgg {
                bytes: "cool doc".as_bytes().to_vec(),
                tags: HashMap::new(),
            }),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.insert_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::internal("").code());
        assert_eq!(status.message(), "internal error");
    }

    #[tokio::test]
    #[cfg(not(feature = "rpc_success"))]
    async fn update_doc_failed() {
        let mut req = Request::new(UpdateDocRequest {
            collection: "foo".to_string(),
            id: TEST_ID_STRING.to_string(),
            version: TEST_VERSION_STRING.to_string(),
            doc: Some(DocumentEgg {
                bytes: "cool doc".as_bytes().to_vec(),
                tags: HashMap::new(),
            }),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.update_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::internal("").code());
        assert_eq!(status.message(), "internal error");
    }

    #[tokio::test]
    #[cfg(not(feature = "rpc_success"))]
    async fn find_doc_by_id_failed() {
        let mut req = Request::new(FindDocByIdRequest {
            collection: "foo".to_string(),
            id: TEST_ID_STRING.to_string(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.find_doc_by_id(req).await.unwrap_err();
        assert_eq!(status.code(), Status::internal("").code());
        assert_eq!(status.message(), "internal error");
    }

    #[tokio::test]
    #[cfg(not(feature = "rpc_success"))]
    async fn find_doc_failed() {
        let mut tags = HashMap::new();
        tags.insert("foo".to_string(), "bar".to_string());
        let mut req = Request::new(FindDocRequest {
            collection: "foo".to_string(),
            tags,
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.find_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::internal("").code());
        assert_eq!(status.message(), "internal error");
    }

    #[tokio::test]
    async fn insert_doc_validates_collection_non_empty() {
        let mut req = Request::new(InsertDocRequest {
            collection: "".to_string(),
            doc: present_doc_egg(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.insert_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "collection is required");
    }

    #[tokio::test]
    async fn insert_doc_validates_db_name_non_empty() {
        let mut req = Request::new(InsertDocRequest {
            collection: "foo".to_string(),
            doc: present_doc_egg(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static(""));
        let status = UNIT_UNDER_TEST.insert_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "db_namespace is required");
    }

    #[tokio::test]
    async fn insert_doc_validates_doc_present() {
        let mut req = Request::new(InsertDocRequest {
            collection: "foo".to_string(),
            doc: None,
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.insert_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "document is required");
    }

    #[tokio::test]
    async fn update_doc_validates_collection_present() {
        let mut req = Request::new(UpdateDocRequest {
            collection: "".to_string(),
            id: TEST_ID_STRING.to_string(),
            version: "123".to_string(),
            doc: present_doc_egg(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.update_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "collection is required");
    }

    #[tokio::test]
    async fn update_doc_validates_db_name_present() {
        let req = Request::new(UpdateDocRequest {
            collection: "foo".to_string(),
            id: TEST_ID_STRING.to_string(),
            version: "123".to_string(),
            doc: present_doc_egg(),
        });
        let status = UNIT_UNDER_TEST.update_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "db_namespace is required");
    }

    #[tokio::test]
    async fn update_doc_validates_id() {
        let mut req = Request::new(UpdateDocRequest {
            collection: "foo".to_string(),
            id: "".to_string(),
            version: "123".to_string(),
            doc: present_doc_egg(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.update_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "id is required");
    }

    #[tokio::test]
    async fn update_doc_validates_version() {
        let mut req = Request::new(UpdateDocRequest {
            collection: "foo".to_string(),
            id: TEST_ID_STRING.to_string(),
            version: "".to_string(),
            doc: present_doc_egg(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.update_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "version is required");
    }

    #[tokio::test]
    async fn find_by_id_validates_collection() {
        let mut req = Request::new(FindDocByIdRequest {
            collection: "".to_string(),
            id: TEST_ID_STRING.to_string(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.find_doc_by_id(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "collection is required");
    }

    #[tokio::test]
    async fn find_by_id_validates_db_name() {
        let req = Request::new(FindDocByIdRequest {
            collection: "foo".to_string(),
            id: TEST_ID_STRING.to_string(),
        });
        let status = UNIT_UNDER_TEST.find_doc_by_id(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "db_namespace is required");
    }

    #[tokio::test]
    async fn find_by_id_validates_id() {
        let mut req = Request::new(FindDocByIdRequest {
            collection: "foo".to_string(),
            id: "".to_string(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.find_doc_by_id(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "id is required");
    }

    #[tokio::test]
    async fn find_by_tags_validates_collection() {
        let mut tags = HashMap::new();
        tags.insert("foo".to_string(), "bar".to_string());
        let mut req = Request::new(FindDocRequest {
            collection: "".to_string(),
            tags,
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.find_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "collection is required");
    }

    #[tokio::test]
    async fn find_by_tags_validates_db_name() {
        let mut tags = HashMap::new();
        tags.insert("foo".to_string(), "bar".to_string());
        let mut req = Request::new(FindDocRequest {
            collection: "foo".to_string(),
            tags,
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static(""));
        let status = UNIT_UNDER_TEST.find_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "db_namespace is required");
    }

    #[tokio::test]
    async fn find_by_tags_validates_tags() {
        let mut req = Request::new(FindDocRequest {
            collection: "foo".to_string(),
            tags: HashMap::new(),
        });
        req.metadata_mut()
            .append(DB_NAME_KEY, MetadataValue::from_static("test"));
        let status = UNIT_UNDER_TEST.find_doc(req).await.unwrap_err();
        assert_eq!(status.code(), Status::invalid_argument("").code());
        assert_eq!(status.message(), "tags are required");
    }
}
