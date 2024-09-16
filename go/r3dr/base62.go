package main

import (
	"encoding/binary"
	"errors"
	"math"
	"math/big"
)

func ToBase62(id int64) (string, error) {
	if id < 0 {
		return "", errors.New("id is negative")
	}
	if id <= math.MaxInt16 {
		return Int16ToBase62(int16(id)), nil
	}
	if id <= math.MaxInt32 {
		return Int32ToBase62(int32(id)), nil
	}
	return Int64ToBase62(id), nil
}

func Int64ToBase62(id int64) string {
	var i big.Int

	bytes := make([]byte, 8)
	binary.LittleEndian.PutUint64(bytes, uint64(id))

	i.SetBytes(bytes)
	return i.Text(62)
}

func Int32ToBase62(id int32) string {
	var i big.Int

	bytes := make([]byte, 4)
	binary.LittleEndian.PutUint32(bytes, uint32(id))

	i.SetBytes(bytes)
	return i.Text(62)
}

func Int16ToBase62(id int16) string {
	var i big.Int

	bytes := make([]byte, 2)
	binary.LittleEndian.PutUint16(bytes, uint16(id))

	i.SetBytes(bytes)
	return i.Text(62)
}
