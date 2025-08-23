import torch
import torch.nn as nn
import json
from typing import Dict, List, Tuple, Optional, Any
from pathlib import Path
import time


class InferenceEngine:
    """PyTorch model inference engine with config-based model loading"""
    
    def __init__(self):
        self.model: Optional[nn.Module] = None
        self.config: Optional[Dict] = None
        self.device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    
    def load_model(self, config_path: str, weights_path: str) -> None:
        """Load model from config and weights files"""
        self.config = self._load_config(config_path)
        self.model = self._build_model_from_config()
        self._load_weights(weights_path)
        self.model.to(self.device)
        self.model.eval()
    
    def _load_config(self, path: str) -> Dict:
        """Load model configuration from JSON file"""
        with open(path, 'r') as f:
            return json.load(f)
    
    def _build_model_from_config(self) -> nn.Module:
        """Build PyTorch model from configuration"""
        layers = []
        
        for layer_config in self.config['layers']:
            layer = self._create_layer(layer_config)
            if layer:
                layers.append(layer)
        
        return nn.Sequential(*layers)
    
    def _create_layer(self, config: Dict) -> Optional[nn.Module]:
        """Create a PyTorch layer from configuration"""
        layer_type = config['type']
        params = config['params']
        
        if layer_type == 'Dense':
            layer = nn.Linear(
                int(params['input_size']), 
                int(params['output_size'])
            )
            # Add activation if specified
            if 'activation' in params:
                activation = self._create_activation(params['activation'])
                if activation:
                    return nn.Sequential(layer, activation)
            return layer
            
        elif layer_type == 'Conv2D':
            kernel_size = params['kernel_size']
            if isinstance(kernel_size, list):
                kernel_size = tuple(kernel_size)
            
            return nn.Conv2d(
                in_channels=int(params['in_channels']),
                out_channels=int(params['out_channels']),
                kernel_size=kernel_size,
                stride=int(params['stride']),
                padding=params['padding'],
                bias=params.get('use_bias', True)
            )
            
        elif layer_type == 'MaxPool2D':
            pool_size = params['pool_size']
            if isinstance(pool_size, list):
                pool_size = tuple(pool_size)
            
            return nn.MaxPool2d(
                kernel_size=pool_size,
                stride=int(params['stride']),
                padding=0 if params['padding'] == 'valid' else 'same'
            )
            
        elif layer_type == 'Flatten':
            return nn.Flatten()
            
        elif layer_type == 'Dropout':
            return nn.Dropout(float(params['rate']))
            
        elif layer_type == 'BatchNorm':
            return nn.BatchNorm1d(int(params['size']))
        
        return None
    
    def _create_activation(self, name: str) -> Optional[nn.Module]:
        """Create activation function from name"""
        activations = {
            'ReLU': nn.ReLU(),
            'Sigmoid': nn.Sigmoid(), 
            'Tanh': nn.Tanh(),
            'Softmax': nn.Softmax(dim=-1),
            'GELU': nn.GELU()
        }
        return activations.get(name)
    
    def _load_weights(self, path: str) -> None:
        """Load model weights from file"""
        # For simplicity, assume it's a PyTorch state dict
        try:
            state_dict = torch.load(path, map_location=self.device)
            self.model.load_state_dict(state_dict)
        except:
            # Fallback to custom JSON format from Go implementation
            with open(path, 'r') as f:
                weights_data = json.load(f)
            self._load_weights_from_json(weights_data)
    
    def _load_weights_from_json(self, weights_data: Dict) -> None:
        """Load weights from custom JSON format"""
        # This would need custom logic to map JSON weights to PyTorch parameters
        # For now, initialize randomly as a placeholder
        pass
    
    def predict(self, input_tensor: torch.Tensor) -> torch.Tensor:
        """Run inference on input tensor"""
        if self.model is None:
            raise ValueError("Model not loaded")
        
        if not self._validate_input_shape(input_tensor.shape):
            raise ValueError(f"Invalid input shape: expected {self.config['input_shape']}, got {input_tensor.shape}")
        
        input_tensor = input_tensor.to(self.device)
        
        with torch.no_grad():
            output = self.model(input_tensor)
        
        return output
    
    def predict_batch(self, inputs: List[torch.Tensor]) -> List[torch.Tensor]:
        """Run batch inference"""
        results = []
        for input_tensor in inputs:
            result = self.predict(input_tensor)
            results.append(result)
        return results
    
    def predict_class(self, input_tensor: torch.Tensor) -> Tuple[int, float]:
        """Predict class with confidence"""
        output = self.predict(input_tensor)
        probabilities = torch.softmax(output, dim=-1)
        confidence, predicted_class = torch.max(probabilities, dim=-1)
        
        return predicted_class.squeeze().item(), confidence.squeeze().item()
    
    def predict_class_name(self, input_tensor: torch.Tensor) -> Tuple[str, float]:
        """Predict class name with confidence"""
        if 'classes' not in self.config:
            raise ValueError("No class names defined in model config")
        
        class_idx, confidence = self.predict_class(input_tensor)
        class_name = self.config['classes'][class_idx]
        
        return class_name, confidence
    
    def predict_top_k(self, input_tensor: torch.Tensor, k: int = 5) -> Tuple[List[int], List[float]]:
        """Get top-k predictions"""
        output = self.predict(input_tensor)
        probabilities = torch.softmax(output, dim=-1)
        
        top_probs, top_indices = torch.topk(probabilities, k)
        
        return top_indices.squeeze().tolist(), top_probs.squeeze().tolist()
    
    def warmup(self, num_iterations: int = 10) -> float:
        """Warmup model with dummy inputs and return average inference time"""
        if self.model is None:
            raise ValueError("Model not loaded")
        
        # Create dummy input based on config
        dummy_shape = [1] + self.config['input_shape'][1:]  # Add batch dimension
        dummy_input = torch.randn(*dummy_shape, device=self.device)
        
        # Warmup iterations
        start_time = time.time()
        for _ in range(num_iterations):
            with torch.no_grad():
                _ = self.model(dummy_input)
        
        total_time = time.time() - start_time
        avg_time = total_time / num_iterations
        
        return avg_time
    
    def benchmark(self, input_tensor: torch.Tensor, num_runs: int = 100) -> Dict[str, float]:
        """Benchmark inference performance"""
        if self.model is None:
            raise ValueError("Model not loaded")
        
        input_tensor = input_tensor.to(self.device)
        
        # Warmup
        for _ in range(10):
            with torch.no_grad():
                _ = self.model(input_tensor)
        
        # Benchmark
        times = []
        for _ in range(num_runs):
            start_time = time.time()
            with torch.no_grad():
                _ = self.model(input_tensor)
            times.append(time.time() - start_time)
        
        return {
            'avg_time': sum(times) / len(times),
            'min_time': min(times),
            'max_time': max(times),
            'std_time': torch.tensor(times).std().item()
        }
    
    def _validate_input_shape(self, shape: Tuple[int, ...]) -> bool:
        """Validate input tensor shape"""
        expected_shape = self.config['input_shape']
        
        # Skip batch dimension in validation
        if len(shape) != len(expected_shape):
            return False
        
        for i in range(1, len(shape)):
            if shape[i] != expected_shape[i]:
                return False
        
        return True
    
    def get_model_info(self) -> Dict:
        """Get model configuration info"""
        return self.config.copy() if self.config else {}
    
    def save_model(self, weights_path: str) -> None:
        """Save model weights"""
        if self.model is None:
            raise ValueError("No model to save")
        
        torch.save(self.model.state_dict(), weights_path)


def create_sample_config() -> Dict:
    """Create a sample model configuration for testing"""
    return {
        "version": "1.0",
        "model_type": "classification",
        "input_shape": [1, 784],  # MNIST-like
        "output_shape": [10],
        "classes": [str(i) for i in range(10)],
        "layers": [
            {
                "type": "Dense",
                "name": "fc1", 
                "params": {
                    "input_size": 784,
                    "output_size": 128,
                    "activation": "ReLU"
                }
            },
            {
                "type": "Dropout",
                "name": "dropout1",
                "params": {
                    "rate": 0.2
                }
            },
            {
                "type": "Dense",
                "name": "fc2",
                "params": {
                    "input_size": 128,
                    "output_size": 10,
                    "activation": "Softmax"
                }
            }
        ]
    }