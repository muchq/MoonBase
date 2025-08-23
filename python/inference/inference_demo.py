import torch
import json
import tempfile
import os
from python.inference.inference import InferenceEngine, create_sample_config


def main():
    print("PyTorch Inference Engine Demo")
    print("=" * 40)
    
    # Create temporary files for config and weights
    with tempfile.TemporaryDirectory() as temp_dir:
        config_path = os.path.join(temp_dir, "model_config.json")
        weights_path = os.path.join(temp_dir, "model_weights.pth")
        
        # Create and save sample config
        config = create_sample_config()
        with open(config_path, 'w') as f:
            json.dump(config, f, indent=2)
        print(f"Created sample config: {config['model_type']} model")
        
        # Create a sample model that matches config structure and save its weights  
        # Build model matching the config to ensure state dict compatibility
        temp_engine = InferenceEngine()
        temp_engine.config = config
        model = temp_engine._build_model_from_config()
        
        # Initialize with random weights
        def init_weights(m):
            if isinstance(m, torch.nn.Linear):
                torch.nn.init.xavier_uniform_(m.weight)
                torch.nn.init.zeros_(m.bias)
        
        model.apply(init_weights)
        torch.save(model.state_dict(), weights_path)
        print("Created and saved sample model weights")
        
        # Initialize inference engine
        engine = InferenceEngine()
        
        try:
            # Load model
            engine.load_model(config_path, weights_path)
            print("✓ Model loaded successfully")
            
            # Print model info
            model_info = engine.get_model_info()
            print(f"Model type: {model_info['model_type']}")
            print(f"Input shape: {model_info['input_shape']}")
            print(f"Output shape: {model_info['output_shape']}")
            print(f"Number of classes: {len(model_info.get('classes', []))}")
            
            # Create sample input (MNIST-like)
            sample_input = torch.randn(1, 784)
            print(f"Sample input shape: {sample_input.shape}")
            
            # Test basic prediction
            output = engine.predict(sample_input)
            print(f"Raw output shape: {output.shape}")
            print(f"Raw output: {output.flatten()[:5].tolist()}")  # Show first 5 values
            
            # Test class prediction
            predicted_class, confidence = engine.predict_class(sample_input)
            print(f"Predicted class: {predicted_class}, Confidence: {confidence:.4f}")
            
            # Test class name prediction
            class_name, confidence = engine.predict_class_name(sample_input)
            print(f"Predicted class name: '{class_name}', Confidence: {confidence:.4f}")
            
            # Test top-k prediction
            top_classes, top_probs = engine.predict_top_k(sample_input, k=3)
            print("Top 3 predictions:")
            for i, (cls, prob) in enumerate(zip(top_classes, top_probs)):
                print(f"  {i+1}. Class {cls}: {prob:.4f}")
            
            # Test warmup
            print("\nTesting warmup...")
            avg_time = engine.warmup(num_iterations=10)
            print(f"Average warmup time: {avg_time*1000:.2f}ms")
            
            # Test benchmark
            print("\nRunning benchmark...")
            benchmark_results = engine.benchmark(sample_input, num_runs=50)
            print(f"Benchmark results:")
            print(f"  Average time: {benchmark_results['avg_time']*1000:.2f}ms")
            print(f"  Min time: {benchmark_results['min_time']*1000:.2f}ms")
            print(f"  Max time: {benchmark_results['max_time']*1000:.2f}ms")
            print(f"  Std dev: {benchmark_results['std_time']*1000:.2f}ms")
            
            # Test batch prediction
            print("\nTesting batch prediction...")
            batch_inputs = [torch.randn(1, 784) for _ in range(3)]
            batch_outputs = engine.predict_batch(batch_inputs)
            print(f"Processed batch of {len(batch_inputs)} inputs")
            for i, output in enumerate(batch_outputs):
                cls, conf = engine.predict_class(batch_inputs[i])
                print(f"  Input {i+1}: Class {cls}, Confidence: {conf:.4f}")
            
            print("\n✓ All tests completed successfully!")
            
        except Exception as e:
            print(f"✗ Error: {e}")
            raise


if __name__ == "__main__":
    main()