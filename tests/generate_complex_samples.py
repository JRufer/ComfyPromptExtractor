#!/usr/bin/env python3
import json
import os
from PIL import Image, PngImagePlugin

# 1. SDXL with text_g and text_l
sdxl_prompt = {
    "1": {
        "class_type": "CLIPTextEncodeSDXL",
        "inputs": {
            "text_g": "a stunning digital painting of a cosmic nebula lion",
            "text_l": "a stunning digital painting of a cosmic nebula lion, 8k resolution, trending on artstation"
        }
    },
    "2": {
        "class_type": "CLIPTextEncodeSDXL",
        "inputs": {
            "text_g": "bad quality, blurry",
            "text_l": "bad quality, blurry, watermark"
        }
    },
    "3": {
        "class_type": "KSampler",
        "inputs": {
            "positive": ["1", 0],
            "negative": ["2", 0]
        }
    }
}

# 2. Flux with FluxGuidance intermediate conditioning node and primitive text node
flux_prompt = {
    "10": {
        "class_type": "PrimitiveNode",
        "inputs": {
            "value": "futuristic cyberpunk city street at night in rain, glowing neon signs"
        }
    },
    "11": {
        "class_type": "CLIPTextEncode",
        "inputs": {
            "text": ["10", 0]
        }
    },
    "12": {
        "class_type": "FluxGuidance",
        "inputs": {
            "conditioning": ["11", 0],
            "guidance": 3.5
        }
    },
    "13": {
        "class_type": "KSampler",
        "inputs": {
            "positive": ["12", 0],
            "negative": ["14", 0]
        }
    },
    "14": {
        "class_type": "CLIPTextEncode",
        "inputs": {
            "text": "ugly, dark, distorted"
        }
    }
}

img = Image.new("RGB", (32, 32), color=(10, 10, 10))

meta1 = PngImagePlugin.PngInfo()
meta1.add_text("prompt", json.dumps(sdxl_prompt))
img.save("tests/sample_sdxl.png", pnginfo=meta1)

meta2 = PngImagePlugin.PngInfo()
meta2.add_text("prompt", json.dumps(flux_prompt))
img.save("tests/sample_flux.png", pnginfo=meta2)

print("Generated sample_sdxl.png and sample_flux.png")
