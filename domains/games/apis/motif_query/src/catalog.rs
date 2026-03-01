use datafusion::arrow::datatypes::{DataType, Field, Schema, SchemaRef};
use datafusion::datasource::file_format::parquet::ParquetFormat;
use datafusion::datasource::listing::ListingOptions;
use datafusion::error::Result as DFResult;
use datafusion::prelude::SessionContext;
use std::sync::Arc;

pub async fn build_context(data_dir: &str) -> DFResult<SessionContext> {
    let ctx = SessionContext::new();
    register_table(&ctx, data_dir, "game_features", game_features_schema()).await?;
    register_table(&ctx, data_dir, "motif_occurrences", motif_occurrences_schema()).await?;
    register_table(&ctx, data_dir, "game_pgns", game_pgns_schema()).await?;
    Ok(ctx)
}

async fn register_table(
    ctx: &SessionContext,
    data_dir: &str,
    table_name: &str,
    schema: SchemaRef,
) -> DFResult<()> {
    let path = format!("{}/{}", data_dir, table_name);
    let options = ListingOptions::new(Arc::new(ParquetFormat::default()))
        .with_file_extension(".parquet")
        .with_table_partition_cols(vec![
            ("platform".to_string(), DataType::Utf8),
            ("month".to_string(), DataType::Utf8),
        ]);
    ctx.register_listing_table(table_name, &path, options, Some(schema), None)
        .await
}

/// Schema for the game_features table. Partition columns (platform, month) are
/// handled by the listing table and are not included here.
pub fn game_features_schema() -> SchemaRef {
    Arc::new(Schema::new(vec![
        Field::new("game_url", DataType::Utf8, false),
        Field::new("white_username", DataType::Utf8, true),
        Field::new("black_username", DataType::Utf8, true),
        Field::new("white_elo", DataType::Int32, true),
        Field::new("black_elo", DataType::Int32, true),
        Field::new("time_class", DataType::Utf8, true),
        Field::new("eco", DataType::Utf8, true),
        Field::new("result", DataType::Utf8, true),
        Field::new("num_moves", DataType::Int32, true),
        Field::new("played_at", DataType::Utf8, true),
    ]))
}

/// Schema for the motif_occurrences table. Includes attacker, target,
/// is_discovered, and is_mate — required for all ATTACK-derived motifs
/// (fork, checkmate, discovered_attack, discovered_check, double_check).
pub fn motif_occurrences_schema() -> SchemaRef {
    Arc::new(Schema::new(vec![
        Field::new("game_url", DataType::Utf8, false),
        Field::new("ply", DataType::Int32, true),
        Field::new("motif", DataType::Utf8, false),
        Field::new("attacker", DataType::Utf8, true),
        Field::new("target", DataType::Utf8, true),
        Field::new("is_discovered", DataType::Boolean, true),
        Field::new("is_mate", DataType::Boolean, true),
    ]))
}

/// Schema for the game_pgns table. Written during Lichess ingest to enable
/// re-analysis without re-downloading source dumps.
pub fn game_pgns_schema() -> SchemaRef {
    Arc::new(Schema::new(vec![
        Field::new("game_url", DataType::Utf8, false),
        Field::new("pgn", DataType::Utf8, true),
    ]))
}

#[cfg(test)]
mod tests {
    use super::*;
    use datafusion::arrow::array::{Array, BooleanArray, Int32Array, StringArray};
    use datafusion::arrow::record_batch::RecordBatch;
    use parquet::arrow::ArrowWriter;
    use std::fs;
    use tempfile::TempDir;

    /// Write a single RecordBatch to a Hive-partitioned Parquet file under
    /// `base/{table}/platform={platform}/month={month}/data.parquet`.
    fn write_parquet(
        base: &std::path::Path,
        table: &str,
        platform: &str,
        month: &str,
        schema: SchemaRef,
        batch: RecordBatch,
    ) {
        let dir = base
            .join(table)
            .join(format!("platform={platform}"))
            .join(format!("month={month}"));
        fs::create_dir_all(&dir).unwrap();
        let file = fs::File::create(dir.join("data.parquet")).unwrap();
        let mut writer = ArrowWriter::try_new(file, schema, None).unwrap();
        writer.write(&batch).unwrap();
        writer.close().unwrap();
    }

    fn make_game_features(urls: &[&str]) -> RecordBatch {
        let schema = game_features_schema();
        let n = urls.len();
        let nulls_str: Vec<Option<&str>> = vec![None; n];
        RecordBatch::try_new(
            schema,
            vec![
                Arc::new(StringArray::from(urls.to_vec())),
                Arc::new(StringArray::from(nulls_str.clone())),
                Arc::new(StringArray::from(nulls_str.clone())),
                Arc::new(Int32Array::from(vec![None::<i32>; n])),
                Arc::new(Int32Array::from(vec![None::<i32>; n])),
                Arc::new(StringArray::from(nulls_str.clone())),
                Arc::new(StringArray::from(nulls_str.clone())),
                Arc::new(StringArray::from(nulls_str.clone())),
                Arc::new(Int32Array::from(vec![None::<i32>; n])),
                Arc::new(StringArray::from(nulls_str)),
            ],
        )
        .unwrap()
    }

    async fn query_game_urls(ctx: &SessionContext, sql: &str) -> Vec<String> {
        let df = ctx.sql(sql).await.unwrap();
        let batches = df.collect().await.unwrap();
        let mut urls = Vec::new();
        for batch in batches {
            let col = batch.column(0);
            let arr = col.as_any().downcast_ref::<StringArray>().unwrap();
            for i in 0..arr.len() {
                urls.push(arr.value(i).to_string());
            }
        }
        urls.sort();
        urls
    }

    #[tokio::test]
    async fn test_fork_derived_motif() {
        let tmp = TempDir::new().unwrap();
        let data_dir = tmp.path().to_str().unwrap().to_string();

        // url1: two ATTACK rows at the same (ply, attacker) → fork
        // url2: only one ATTACK row → no fork
        let mo_schema = motif_occurrences_schema();
        let mo_batch = RecordBatch::try_new(
            mo_schema.clone(),
            vec![
                Arc::new(StringArray::from(vec!["url1", "url1", "url2"])),
                Arc::new(Int32Array::from(vec![10, 10, 5])),
                Arc::new(StringArray::from(vec!["ATTACK", "ATTACK", "ATTACK"])),
                Arc::new(StringArray::from(vec![
                    Some("N"),
                    Some("N"),
                    Some("B"),
                ])),
                Arc::new(StringArray::from(vec![
                    Some("Re4"),
                    Some("Be7"),
                    Some("Qd5"),
                ])),
                Arc::new(BooleanArray::from(vec![false, false, false])),
                Arc::new(BooleanArray::from(vec![false, false, false])),
            ],
        )
        .unwrap();
        write_parquet(
            tmp.path(),
            "motif_occurrences",
            "chess.com",
            "2024-01",
            mo_schema,
            mo_batch,
        );
        write_parquet(
            tmp.path(),
            "game_features",
            "chess.com",
            "2024-01",
            game_features_schema(),
            make_game_features(&["url1", "url2"]),
        );

        let ctx = build_context(&data_dir).await.unwrap();
        let sql = "SELECT g.game_url FROM game_features g \
            WHERE EXISTS (\
                SELECT 1 FROM motif_occurrences mo \
                WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK' \
                AND mo.is_discovered = FALSE AND mo.attacker IS NOT NULL \
                GROUP BY mo.ply, mo.attacker HAVING COUNT(*) >= 2\
            ) ORDER BY g.game_url";

        let urls = query_game_urls(&ctx, sql).await;
        assert_eq!(urls, vec!["url1"]);
    }

    #[tokio::test]
    async fn test_double_check_derived_motif() {
        let tmp = TempDir::new().unwrap();
        let data_dir = tmp.path().to_str().unwrap().to_string();

        // url1: two ATTACK rows at same ply targeting K% squares → double_check
        // url2: one ATTACK row → no double_check
        let mo_schema = motif_occurrences_schema();
        let mo_batch = RecordBatch::try_new(
            mo_schema.clone(),
            vec![
                Arc::new(StringArray::from(vec!["url1", "url1", "url2"])),
                Arc::new(Int32Array::from(vec![20, 20, 15])),
                Arc::new(StringArray::from(vec!["ATTACK", "ATTACK", "ATTACK"])),
                Arc::new(StringArray::from(vec![Some("R"), Some("B"), Some("R")])),
                Arc::new(StringArray::from(vec![
                    Some("Ke1"),
                    Some("Ke1"),
                    Some("Ke1"),
                ])),
                Arc::new(BooleanArray::from(vec![false, false, false])),
                Arc::new(BooleanArray::from(vec![false, false, false])),
            ],
        )
        .unwrap();
        write_parquet(
            tmp.path(),
            "motif_occurrences",
            "chess.com",
            "2024-01",
            mo_schema,
            mo_batch,
        );
        write_parquet(
            tmp.path(),
            "game_features",
            "chess.com",
            "2024-01",
            game_features_schema(),
            make_game_features(&["url1", "url2"]),
        );

        let ctx = build_context(&data_dir).await.unwrap();
        let sql = "SELECT g.game_url FROM game_features g \
            WHERE EXISTS (\
                SELECT 1 FROM motif_occurrences mo \
                WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK' \
                AND (mo.target LIKE 'K%' OR mo.target LIKE 'k%') \
                GROUP BY mo.ply HAVING COUNT(*) >= 2\
            ) ORDER BY g.game_url";

        let urls = query_game_urls(&ctx, sql).await;
        assert_eq!(urls, vec!["url1"]);
    }

    #[tokio::test]
    async fn test_sequence_pin_then_check() {
        let tmp = TempDir::new().unwrap();
        let data_dir = tmp.path().to_str().unwrap().to_string();

        // url1: PIN at ply 10, CHECK at ply 12 → ply diff = 2 → sequence matches
        // url2: PIN at ply 10, CHECK at ply 15 → ply diff = 5 → no match
        let mo_schema = motif_occurrences_schema();
        let mo_batch = RecordBatch::try_new(
            mo_schema.clone(),
            vec![
                Arc::new(StringArray::from(vec!["url1", "url1", "url2", "url2"])),
                Arc::new(Int32Array::from(vec![10, 12, 10, 15])),
                Arc::new(StringArray::from(vec!["PIN", "CHECK", "PIN", "CHECK"])),
                Arc::new(StringArray::from(vec![
                    None::<&str>,
                    None,
                    None,
                    None,
                ])),
                Arc::new(StringArray::from(vec![
                    None::<&str>,
                    None,
                    None,
                    None,
                ])),
                Arc::new(BooleanArray::from(vec![
                    None::<bool>,
                    None,
                    None,
                    None,
                ])),
                Arc::new(BooleanArray::from(vec![
                    None::<bool>,
                    None,
                    None,
                    None,
                ])),
            ],
        )
        .unwrap();
        write_parquet(
            tmp.path(),
            "motif_occurrences",
            "chess.com",
            "2024-01",
            mo_schema,
            mo_batch,
        );
        write_parquet(
            tmp.path(),
            "game_features",
            "chess.com",
            "2024-01",
            game_features_schema(),
            make_game_features(&["url1", "url2"]),
        );

        let ctx = build_context(&data_dir).await.unwrap();
        let sql = "SELECT g.game_url FROM game_features g \
            WHERE EXISTS (\
                SELECT 1 FROM \
                (SELECT game_url, ply FROM motif_occurrences WHERE motif = 'PIN') sq1 \
                JOIN (SELECT game_url, ply FROM motif_occurrences WHERE motif = 'CHECK') sq2 \
                ON sq2.game_url = sq1.game_url AND sq2.ply = sq1.ply + 2 \
                WHERE sq1.game_url = g.game_url\
            ) ORDER BY g.game_url";

        let urls = query_game_urls(&ctx, sql).await;
        assert_eq!(urls, vec!["url1"]);
    }

    #[tokio::test]
    async fn test_stored_motif_exists() {
        let tmp = TempDir::new().unwrap();
        let data_dir = tmp.path().to_str().unwrap().to_string();

        // url1 has a PIN motif; url2 does not
        let mo_schema = motif_occurrences_schema();
        let mo_batch = RecordBatch::try_new(
            mo_schema.clone(),
            vec![
                Arc::new(StringArray::from(vec!["url1"])),
                Arc::new(Int32Array::from(vec![8])),
                Arc::new(StringArray::from(vec!["PIN"])),
                Arc::new(StringArray::from(vec![None::<&str>])),
                Arc::new(StringArray::from(vec![None::<&str>])),
                Arc::new(BooleanArray::from(vec![None::<bool>])),
                Arc::new(BooleanArray::from(vec![None::<bool>])),
            ],
        )
        .unwrap();
        write_parquet(
            tmp.path(),
            "motif_occurrences",
            "chess.com",
            "2024-01",
            mo_schema,
            mo_batch,
        );
        write_parquet(
            tmp.path(),
            "game_features",
            "chess.com",
            "2024-01",
            game_features_schema(),
            make_game_features(&["url1", "url2"]),
        );

        let ctx = build_context(&data_dir).await.unwrap();
        let sql = "SELECT g.game_url FROM game_features g \
            WHERE EXISTS (\
                SELECT 1 FROM motif_occurrences mo \
                WHERE mo.game_url = g.game_url AND mo.motif = 'PIN'\
            ) ORDER BY g.game_url";

        let urls = query_game_urls(&ctx, sql).await;
        assert_eq!(urls, vec!["url1"]);
    }
}
