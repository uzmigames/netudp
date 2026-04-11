# 17. Entity System & Delta Sync

3D entities with FVector(X,Y,Z) + FRotator(Pitch,Yaw,Roll). Quantized sync: position → 6 bytes, rotation → 6 bytes (vs 24 bytes uncompressed). Delta sync packet type defined (ServerPackets.DeltaSync) but handler not fully implemented. StructPool<Entity> for fixed-capacity entity storage with NativeMemory.
