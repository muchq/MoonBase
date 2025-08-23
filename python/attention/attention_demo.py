import torch
from attention import TransformerBlock, PositionalEncoding

def main():
    # Demo parameters
    batch_size = 2
    seq_len = 10
    d_model = 512
    num_heads = 8
    d_ff = 2048
    
    # Create sample data
    x = torch.randn(batch_size, seq_len, d_model)
    print(f"Input shape: {x.shape}")
    
    # Add positional encoding
    pos_enc = PositionalEncoding(d_model)
    x_with_pos = pos_enc(x)
    print(f"After positional encoding: {x_with_pos.shape}")
    
    # Create transformer block
    transformer = TransformerBlock(d_model, num_heads, d_ff)
    
    # Forward pass
    output = transformer(x_with_pos)
    print(f"Output shape: {output.shape}")
    print("Transformer block demo completed successfully!")

if __name__ == "__main__":
    main()