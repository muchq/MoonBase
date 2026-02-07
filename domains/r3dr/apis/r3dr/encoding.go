package main

import (
	"encoding/base64"
	"encoding/binary"
	"errors"
	"math"
)

func EncodeId(id int64) (string, error) {
	if id < 0 {
		return "", errors.New("id is negative")
	}
	bytes := IntToBytes(id)
	return base64.RawURLEncoding.EncodeToString(bytes), nil
}

func IntToBytes(id int64) []byte {
	if id <= math.MaxInt16 {
		return Int16ToBytes(id)
	}
	if id <= math.MaxInt32 {
		return Int32ToBytes(id)
	}
	return Int64ToBytes(id)
}

func Int16ToBytes(id int64) []byte {
	b := make([]byte, 2)
	binary.LittleEndian.PutUint16(b, uint16(id))
	return b
}

func Int32ToBytes(id int64) []byte {
	b := make([]byte, 4)
	binary.LittleEndian.PutUint32(b, uint32(id))
	return b
}

func Int64ToBytes(id int64) []byte {
	b := make([]byte, 8)
	binary.LittleEndian.PutUint64(b, uint64(id))
	return b
}
