package utils

import (
	"math"
	"testing"
)

func TestNewTensor(t *testing.T) {
	tensor := NewTensor(2, 3, 4)
	
	if len(tensor.Shape) != 3 {
		t.Errorf("Expected shape length 3, got %d", len(tensor.Shape))
	}
	
	if tensor.Size() != 24 {
		t.Errorf("Expected size 24, got %d", tensor.Size())
	}
}

func TestTensorGetSet(t *testing.T) {
	tensor := NewTensor(2, 3)
	
	tensor.Set(5.0, 1, 2)
	val := tensor.Get(1, 2)
	
	if val != 5.0 {
		t.Errorf("Expected 5.0, got %f", val)
	}
}

func TestTensorAdd(t *testing.T) {
	t1 := NewTensorFromData([]float64{1, 2, 3, 4}, 2, 2)
	t2 := NewTensorFromData([]float64{5, 6, 7, 8}, 2, 2)
	
	result := t1.Add(t2)
	
	expected := []float64{6, 8, 10, 12}
	for i, v := range expected {
		if math.Abs(result.Data[i]-v) > 1e-6 {
			t.Errorf("At index %d: expected %f, got %f", i, v, result.Data[i])
		}
	}
}

func TestTensorMatMul(t *testing.T) {
	t1 := NewTensorFromData([]float64{1, 2, 3, 4}, 2, 2)
	t2 := NewTensorFromData([]float64{5, 6, 7, 8}, 2, 2)
	
	result := t1.MatMul(t2)
	
	expected := []float64{19, 22, 43, 50}
	for i, v := range expected {
		if math.Abs(result.Data[i]-v) > 1e-6 {
			t.Errorf("At index %d: expected %f, got %f", i, v, result.Data[i])
		}
	}
}

func TestTensorTranspose(t *testing.T) {
	tensor := NewTensorFromData([]float64{1, 2, 3, 4, 5, 6}, 2, 3)
	result := tensor.Transpose()
	
	if result.Shape[0] != 3 || result.Shape[1] != 2 {
		t.Errorf("Expected shape [3,2], got %v", result.Shape)
	}
	
	expected := []float64{1, 4, 2, 5, 3, 6}
	for i, v := range expected {
		if math.Abs(result.Data[i]-v) > 1e-6 {
			t.Errorf("At index %d: expected %f, got %f", i, v, result.Data[i])
		}
	}
}

func TestTensorReshape(t *testing.T) {
	tensor := NewTensorFromData([]float64{1, 2, 3, 4, 5, 6}, 2, 3)
	result := tensor.Reshape(3, 2)
	
	if result.Shape[0] != 3 || result.Shape[1] != 2 {
		t.Errorf("Expected shape [3,2], got %v", result.Shape)
	}
	
	for i := range tensor.Data {
		if result.Data[i] != tensor.Data[i] {
			t.Errorf("Data mismatch at index %d", i)
		}
	}
}

func TestXavierInit(t *testing.T) {
	tensor := XavierInit(100, 50)
	
	mean := tensor.Mean()
	if math.Abs(mean) > 0.1 {
		t.Errorf("Xavier init mean too far from 0: %f", mean)
	}
	
	variance := 0.0
	for _, v := range tensor.Data {
		diff := v - mean
		variance += diff * diff
	}
	variance /= float64(len(tensor.Data))
	
	expectedVar := 2.0 / 100.0
	if math.Abs(variance-expectedVar) > expectedVar {
		t.Errorf("Xavier init variance too far from expected: got %f, expected ~%f", variance, expectedVar)
	}
}