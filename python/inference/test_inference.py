import torch
import torch.nn as nn
import json
import tempfile
import os
import pytest
from unittest.mock import patch, mock_open
from python.inference.inference import InferenceEngine, create_sample_config


class TestInferenceEngine:
    
    @pytest.fixture
    def sample_config(self):
        return create_sample_config()
    
    @pytest.fixture
    def temp_files(self):
        """Create temporary config and weights files"""
        temp_dir = tempfile.mkdtemp()
        config_path = os.path.join(temp_dir, "config.json")
        weights_path = os.path.join(temp_dir, "weights.pth")
        
        # Create sample config
        config = create_sample_config()
        with open(config_path, 'w') as f:
            json.dump(config, f)
        
        # Create and save sample model
        model = nn.Sequential(
            nn.Linear(784, 128),
            nn.ReLU(),
            nn.Dropout(0.2),
            nn.Linear(128, 10),
            nn.Softmax(dim=-1)
        )
        torch.save(model.state_dict(), weights_path)
        
        yield config_path, weights_path
        
        # Cleanup
        os.remove(config_path)
        os.remove(weights_path)
        os.rmdir(temp_dir)
    
    @pytest.fixture
    def loaded_engine(self, temp_files):
        """Engine with model already loaded"""
        config_path, weights_path = temp_files
        engine = InferenceEngine()
        engine.load_model(config_path, weights_path)
        return engine
    
    def test_init(self):
        """Test engine initialization"""
        engine = InferenceEngine()
        assert engine.model is None
        assert engine.config is None
        assert engine.device is not None
    
    def test_load_config(self, sample_config):
        """Test config loading"""
        engine = InferenceEngine()
        
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            json.dump(sample_config, f)
            config_path = f.name
        
        try:
            config = engine._load_config(config_path)
            assert config == sample_config
        finally:
            os.unlink(config_path)
    
    def test_load_config_file_not_found(self):
        """Test config loading with non-existent file"""
        engine = InferenceEngine()
        
        with pytest.raises(FileNotFoundError):
            engine._load_config("nonexistent.json")
    
    def test_create_activation(self):
        """Test activation function creation"""
        engine = InferenceEngine()
        
        # Test valid activations
        relu = engine._create_activation("ReLU")
        assert isinstance(relu, nn.ReLU)
        
        sigmoid = engine._create_activation("Sigmoid")
        assert isinstance(sigmoid, nn.Sigmoid)
        
        # Test invalid activation
        invalid = engine._create_activation("InvalidActivation")
        assert invalid is None
    
    def test_create_layer_dense(self):
        """Test Dense layer creation"""
        engine = InferenceEngine()
        
        config = {
            "type": "Dense",
            "params": {
                "input_size": 10,
                "output_size": 5
            }
        }
        
        layer = engine._create_layer(config)
        assert isinstance(layer, nn.Linear)
        assert layer.in_features == 10
        assert layer.out_features == 5
    
    def test_create_layer_dense_with_activation(self):
        """Test Dense layer with activation"""
        engine = InferenceEngine()
        
        config = {
            "type": "Dense", 
            "params": {
                "input_size": 10,
                "output_size": 5,
                "activation": "ReLU"
            }
        }
        
        layer = engine._create_layer(config)
        assert isinstance(layer, nn.Sequential)
        assert len(layer) == 2
        assert isinstance(layer[0], nn.Linear)
        assert isinstance(layer[1], nn.ReLU)
    
    def test_create_layer_conv2d(self):
        """Test Conv2D layer creation"""
        engine = InferenceEngine()
        
        config = {
            "type": "Conv2D",
            "params": {
                "in_channels": 3,
                "out_channels": 16,
                "kernel_size": [3, 3],
                "stride": 1,
                "padding": "same",
                "use_bias": True
            }
        }
        
        layer = engine._create_layer(config)
        assert isinstance(layer, nn.Conv2d)
        assert layer.in_channels == 3
        assert layer.out_channels == 16
        assert layer.kernel_size == (3, 3)
    
    def test_create_layer_maxpool2d(self):
        """Test MaxPool2D layer creation"""
        engine = InferenceEngine()
        
        config = {
            "type": "MaxPool2D",
            "params": {
                "pool_size": [2, 2],
                "stride": 2,
                "padding": "valid"
            }
        }
        
        layer = engine._create_layer(config)
        assert isinstance(layer, nn.MaxPool2d)
        assert layer.kernel_size == (2, 2)
        assert layer.stride == 2
    
    def test_create_layer_flatten(self):
        """Test Flatten layer creation"""
        engine = InferenceEngine()
        
        config = {"type": "Flatten", "params": {}}
        layer = engine._create_layer(config)
        assert isinstance(layer, nn.Flatten)
    
    def test_create_layer_dropout(self):
        """Test Dropout layer creation"""
        engine = InferenceEngine()
        
        config = {
            "type": "Dropout",
            "params": {"rate": 0.5}
        }
        
        layer = engine._create_layer(config)
        assert isinstance(layer, nn.Dropout)
        assert layer.p == 0.5
    
    def test_create_layer_unknown(self):
        """Test unknown layer type"""
        engine = InferenceEngine()
        
        config = {"type": "UnknownLayer", "params": {}}
        layer = engine._create_layer(config)
        assert layer is None
    
    def test_load_model(self, temp_files):
        """Test full model loading"""
        config_path, weights_path = temp_files
        engine = InferenceEngine()
        
        engine.load_model(config_path, weights_path)
        
        assert engine.model is not None
        assert engine.config is not None
        assert engine.model.training is False  # Should be in eval mode
    
    def test_predict(self, loaded_engine):
        """Test basic prediction"""
        input_tensor = torch.randn(1, 784)
        output = loaded_engine.predict(input_tensor)
        
        assert output is not None
        assert output.shape == torch.Size([1, 10])
        assert torch.all(output >= 0)  # Softmax output should be non-negative
    
    def test_predict_invalid_shape(self, loaded_engine):
        """Test prediction with invalid input shape"""
        input_tensor = torch.randn(1, 100)  # Wrong shape
        
        with pytest.raises(ValueError, match="Invalid input shape"):
            loaded_engine.predict(input_tensor)
    
    def test_predict_no_model(self):
        """Test prediction without loaded model"""
        engine = InferenceEngine()
        input_tensor = torch.randn(1, 784)
        
        with pytest.raises(ValueError, match="Model not loaded"):
            engine.predict(input_tensor)
    
    def test_predict_class(self, loaded_engine):
        """Test class prediction"""
        input_tensor = torch.randn(1, 784)
        predicted_class, confidence = loaded_engine.predict_class(input_tensor)
        
        assert isinstance(predicted_class, int)
        assert 0 <= predicted_class < 10
        assert isinstance(confidence, float)
        assert 0 <= confidence <= 1
    
    def test_predict_class_name(self, loaded_engine):
        """Test class name prediction"""
        input_tensor = torch.randn(1, 784)
        class_name, confidence = loaded_engine.predict_class_name(input_tensor)
        
        assert isinstance(class_name, str)
        assert class_name in [str(i) for i in range(10)]
        assert isinstance(confidence, float)
        assert 0 <= confidence <= 1
    
    def test_predict_class_name_no_classes(self, temp_files):
        """Test class name prediction without class names in config"""
        config_path, weights_path = temp_files
        
        # Load config and remove classes
        with open(config_path, 'r') as f:
            config = json.load(f)
        del config['classes']
        with open(config_path, 'w') as f:
            json.dump(config, f)
        
        engine = InferenceEngine()
        engine.load_model(config_path, weights_path)
        
        input_tensor = torch.randn(1, 784)
        
        with pytest.raises(ValueError, match="No class names defined"):
            engine.predict_class_name(input_tensor)
    
    def test_predict_top_k(self, loaded_engine):
        """Test top-k prediction"""
        input_tensor = torch.randn(1, 784)
        top_indices, top_probs = loaded_engine.predict_top_k(input_tensor, k=3)
        
        assert len(top_indices) == 3
        assert len(top_probs) == 3
        assert all(isinstance(idx, int) for idx in top_indices)
        assert all(isinstance(prob, float) for prob in top_probs)
        assert all(0 <= prob <= 1 for prob in top_probs)
        # Should be sorted by probability (descending)
        assert top_probs == sorted(top_probs, reverse=True)
    
    def test_predict_batch(self, loaded_engine):
        """Test batch prediction"""
        batch_inputs = [torch.randn(1, 784) for _ in range(3)]
        outputs = loaded_engine.predict_batch(batch_inputs)
        
        assert len(outputs) == 3
        for output in outputs:
            assert output.shape == torch.Size([1, 10])
    
    def test_warmup(self, loaded_engine):
        """Test model warmup"""
        avg_time = loaded_engine.warmup(num_iterations=5)
        
        assert isinstance(avg_time, float)
        assert avg_time > 0
    
    def test_warmup_no_model(self):
        """Test warmup without loaded model"""
        engine = InferenceEngine()
        
        with pytest.raises(ValueError, match="Model not loaded"):
            engine.warmup()
    
    def test_benchmark(self, loaded_engine):
        """Test benchmarking"""
        input_tensor = torch.randn(1, 784)
        results = loaded_engine.benchmark(input_tensor, num_runs=5)
        
        assert 'avg_time' in results
        assert 'min_time' in results
        assert 'max_time' in results
        assert 'std_time' in results
        
        assert all(isinstance(v, float) and v >= 0 for v in results.values())
        assert results['min_time'] <= results['avg_time'] <= results['max_time']
    
    def test_benchmark_no_model(self):
        """Test benchmark without loaded model"""
        engine = InferenceEngine()
        input_tensor = torch.randn(1, 784)
        
        with pytest.raises(ValueError, match="Model not loaded"):
            engine.benchmark(input_tensor)
    
    def test_validate_input_shape(self, loaded_engine):
        """Test input shape validation"""
        # Valid shapes
        assert loaded_engine._validate_input_shape((1, 784)) is True
        assert loaded_engine._validate_input_shape((5, 784)) is True  # Different batch size
        
        # Invalid shapes
        assert loaded_engine._validate_input_shape((784,)) is False  # Missing batch dim
        assert loaded_engine._validate_input_shape((1, 100)) is False  # Wrong feature dim
        assert loaded_engine._validate_input_shape((1, 784, 10)) is False  # Extra dim
    
    def test_get_model_info(self, loaded_engine):
        """Test model info retrieval"""
        info = loaded_engine.get_model_info()
        
        assert isinstance(info, dict)
        assert 'version' in info
        assert 'model_type' in info
        assert 'input_shape' in info
        assert 'output_shape' in info
        assert 'layers' in info
    
    def test_get_model_info_no_config(self):
        """Test model info without loaded config"""
        engine = InferenceEngine()
        info = engine.get_model_info()
        
        assert info == {}
    
    def test_save_model(self, loaded_engine, tmp_path):
        """Test model saving"""
        save_path = tmp_path / "saved_model.pth"
        loaded_engine.save_model(str(save_path))
        
        assert save_path.exists()
        
        # Verify we can load the saved weights
        saved_state = torch.load(save_path)
        assert isinstance(saved_state, dict)
    
    def test_save_model_no_model(self):
        """Test saving without loaded model"""
        engine = InferenceEngine()
        
        with pytest.raises(ValueError, match="No model to save"):
            engine.save_model("test.pth")
    
    def test_create_sample_config(self):
        """Test sample config creation"""
        config = create_sample_config()
        
        assert isinstance(config, dict)
        assert 'version' in config
        assert 'model_type' in config
        assert 'input_shape' in config
        assert 'output_shape' in config
        assert 'classes' in config
        assert 'layers' in config
        assert len(config['layers']) > 0


class TestIntegration:
    """Integration tests for the inference engine"""
    
    def test_end_to_end_classification(self):
        """Test complete classification pipeline"""
        # Create a simple model
        model = nn.Sequential(
            nn.Linear(4, 8),
            nn.ReLU(),
            nn.Linear(8, 3),
            nn.Softmax(dim=-1)
        )
        
        # Create config
        config = {
            "version": "1.0",
            "model_type": "classification",
            "input_shape": [1, 4],
            "output_shape": [3],
            "classes": ["class_0", "class_1", "class_2"],
            "layers": [
                {
                    "type": "Dense",
                    "name": "fc1",
                    "params": {"input_size": 4, "output_size": 8, "activation": "ReLU"}
                },
                {
                    "type": "Dense", 
                    "name": "fc2",
                    "params": {"input_size": 8, "output_size": 3, "activation": "Softmax"}
                }
            ]
        }
        
        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = os.path.join(temp_dir, "config.json")
            weights_path = os.path.join(temp_dir, "weights.pth")
            
            # Save config and weights
            with open(config_path, 'w') as f:
                json.dump(config, f)
            torch.save(model.state_dict(), weights_path)
            
            # Load and test
            engine = InferenceEngine()
            engine.load_model(config_path, weights_path)
            
            # Test various prediction methods
            input_data = torch.randn(1, 4)
            
            # Basic prediction
            output = engine.predict(input_data)
            assert output.shape == (1, 3)
            
            # Class prediction
            pred_class, confidence = engine.predict_class(input_data)
            assert 0 <= pred_class <= 2
            assert 0 <= confidence <= 1
            
            # Class name prediction
            class_name, confidence = engine.predict_class_name(input_data)
            assert class_name in ["class_0", "class_1", "class_2"]
            
            # Top-k prediction
            top_classes, top_probs = engine.predict_top_k(input_data, k=2)
            assert len(top_classes) == 2
            assert len(top_probs) == 2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])