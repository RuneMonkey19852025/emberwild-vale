// Emberwild Vale — world implementation.
// Faithful port of the Unity original's WorldBuilder.cs (heightfield, town,
// nodes, stations) and PlayerController.cs (camera-relative movement, zoom,
// melee). Visuals are built from scaled/rotated primitive static meshes with
// flat-shaded toon materials, exactly like the original's primitives.

#include "EmberwildWorld.h"
#include "EmberwildDB.h"
#include "EmberwildGame.h"
#include "EmberwildSystems.h"
#include "EmberwildUI.h"
#include "EmberwildAudio.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "ProceduralMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "KismetProceduralMeshLibrary.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// ================================================================ helpers
FLinearColor AEmberwildWorldBuilder::HexColor(const FString& Hex)
{
	FString S = Hex;
	if (S.StartsWith(TEXT("#"))) S = S.Mid(1);
	if (S.Len() == 6)
	{
		const int32 R = FParse::HexDigit(S[0]) * 16 + FParse::HexDigit(S[1]);
		const int32 G = FParse::HexDigit(S[2]) * 16 + FParse::HexDigit(S[3]);
		const int32 B = FParse::HexDigit(S[4]) * 16 + FParse::HexDigit(S[5]);
		return FLinearColor(R / 255.f, G / 255.f, B / 255.f, 1.f);
	}
	return FLinearColor::White;
}

namespace
{
	// The engine's default cube/sphere/cylinder/plane meshes, loaded once.
	UStaticMesh* PrimMesh(const TCHAR* Path)
	{
		static TMap<FString, UStaticMesh*> Cache;
		if (UStaticMesh** Found = Cache.Find(Path)) return *Found;
	UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Path);
	Cache.Add(Path, M);
		return M;
	}
}

UStaticMesh* EmberwildPrimCube()   { return PrimMesh(TEXT("/Engine/BasicShapes/Cube.Cube")); }
UStaticMesh* EmberwildPrimSphere() { return PrimMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere")); }
UStaticMesh* EmberwildPrimCyl()    { return PrimMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); }
UStaticMesh* EmberwildPrimPlane()  { return PrimMesh(TEXT("/Engine/BasicShapes/Plane.Plane")); }

UMaterialInterface* AEmberwildWorldBuilder::ToonBase()
{
	static UMaterialInterface* Mat = nullptr;
	if (!Mat)
	{
		// BasicShapeMaterial carries a "Color" vector parameter: flat toon look.
		// (Runtime load: these helpers run from BeginPlay, not UObject constructors.)
		Mat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Mat)
		{
			Mat = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
		}
	}
	return Mat;
}

UMaterialInstanceDynamic* AEmberwildWorldBuilder::ToonMID(UObject* Outer, const FString& Hex)
{
	// A flat unlit color material created lazily per color (cached by hex).
	static TMap<FString, UMaterialInstanceDynamic*> Cache;
	if (UMaterialInstanceDynamic** Found = Cache.Find(Hex)) return *Found;

	UMaterialInterface* Base = ToonBase();
	UMaterialInstanceDynamic* MID = nullptr;
	if (Base)
	{
		MID = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
		MID->SetVectorParameterValue(TEXT("Color"), HexColor(Hex));
		MID->SetVectorParameterValue(TEXT("Tint"), HexColor(Hex));
	}
	Cache.Add(Hex, MID);
	return MID;
}

namespace
{
	/** Scaled, colored primitive part parented to a root. */
	UStaticMeshComponent* Part(USceneComponent* Parent, UStaticMesh* Mesh, const FString& Name,
	                           const FVector& RelPos, const FVector& Scale, const FString& Hex,
	                           const FRotator& RelRot = FRotator::ZeroRotator)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Parent->GetOwner());
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Mesh);
		C->SetRelativeLocation(RelPos);
		C->SetRelativeRotation(RelRot);
		C->SetRelativeScale3D(Scale);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (UMaterialInstanceDynamic* MID = AEmberwildWorldBuilder::ToonMID(nullptr, Hex))
			C->SetMaterial(0, MID);
		if (Parent->GetOwner() && Parent->GetOwner()->GetWorld())
			C->RegisterComponent();
		return C;
	}
}

// ================================================================ world builder
AEmberwildWorldBuilder::AEmberwildWorldBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

const TMap<FString, FVector2D>& AEmberwildWorldBuilder::ZoneCenters()
{
	static TMap<FString, FVector2D> Centers = {
		{ TEXT("meadow"),       FVector2D(0.f, 0.f) },
		{ TEXT("ridge"),        FVector2D(70.f, 15.f) },
		{ TEXT("lake"),         FVector2D(-8.f, -78.f) },
		{ TEXT("hollow"),       FVector2D(10.f, 75.f) },
		{ TEXT("hollow_deep"),  FVector2D(55.f, 82.f) },
	};
	return Centers;
}

bool AEmberwildWorldBuilder::InTownPad(float X, float Z, float Pad)
{
	return FMath::Abs(X - TownCX) <= TownHalfW + Pad && FMath::Abs(Z - TownCZ) <= TownHalfH + Pad;
}

float AEmberwildWorldBuilder::GroundHeight(float X, float Z)
{
	float H = FMath::PerlinNoise2D(FVector2D(X * 0.02f, Z * 0.02f)) * 4.f;
	H += FMath::PerlinNoise2D(FVector2D(X * 0.08f, Z * 0.08f)) * 1.2f;

	// ridge plateau in the east
	const float Ridge = FMath::Exp(-FMath::Square((X - 70.f) / 28.f)) * 9.f * FMath::PerlinNoise2D(FVector2D(X * 0.05f, Z * 0.05f));
	H += Ridge;

	// lake basin in the north
	const float Lake = FMath::Exp(-FMath::Square((X + 8.f) / 30.f) - FMath::Square((Z + 78.f) / 26.f)) * 7.f;
	H -= Lake;

	// hollow dip in the south
	const float Hollow = FMath::Exp(-FMath::Square((X - 10.f) / 26.f) - FMath::Square((Z - 75.f) / 30.f)) * 5.f;
	H -= Hollow * 0.5f;

	// island falloff
	const float D = FMath::Max(FMath::Abs(X), FMath::Abs(Z)) / (0.5f * 200.f);
	H -= FMath::Pow(FMath::Clamp(D, 0.f, 1.f), 6.f) * 8.f;

	// Emberpost town pad: flat core with a soft feather
	const float Mx = FMath::Clamp((TownHalfW - FMath::Abs(X - TownCX)) / TownFeather, 0.f, 1.f);
	const float Mz = FMath::Clamp((TownHalfH - FMath::Abs(Z - TownCZ)) / TownFeather, 0.f, 1.f);
	H = FMath::Lerp(H, TownY, Mx * Mz);

	return H;
}

float AEmberwildWorldBuilder::SurfaceHeight(float X, float Z)
{
	float H = GroundHeight(X, Z);
	if (X >= BridgeX0 - 0.1f && X <= BridgeX1 + 0.1f && FMath::Abs(Z - BridgeZ) <= BridgeHalfWidth)
	{
		const float T = FMath::Clamp((X - BridgeX0) / (BridgeX1 - BridgeX0), 0.f, 1.f);
		const float Deck = GroundHeight(X, Z) + 0.2f + FMath::Sin(T * PI) * BridgeArch;
		H = FMath::Max(H, Deck);
	}
	return H;
}

void AEmberwildWorldBuilder::Build(AEmberwildPlayer* Player)
{
	BuildTerrain();
	BuildWater();
	BuildBridge();
	BuildGrass();
	BuildTown();
}

void AEmberwildWorldBuilder::BuildTerrain()
{
	constexpr int32 Seg = 100;
	constexpr float Size = 200.f;

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	const FLinearColor MeadowC = HexColor(TEXT("#7EC850"));
	const FLinearColor RidgeC  = HexColor(TEXT("#C9A06A"));
	const FLinearColor LakeC   = HexColor(TEXT("#5FBF9A"));
	const FLinearColor HollowC = HexColor(TEXT("#3E7D5A"));
	const FLinearColor DeepC   = HexColor(TEXT("#2C5445"));

	Verts.SetNum((Seg + 1) * (Seg + 1));
	Normals.Init(FVector::UpVector, Verts.Num());
	UVs.SetNum(Verts.Num());
	Colors.SetNum(Verts.Num());
	Tangents.SetNum(Verts.Num());

	int32 I = 0;
	for (int32 Zi = 0; Zi <= Seg; ++Zi)
	{
		for (int32 Xi = 0; Xi <= Seg; ++Xi)
		{
			const float X = (Xi / (float)Seg - 0.5f) * Size;
			const float Z = (Zi / (float)Seg - 0.5f) * Size;
			const float Y = GroundHeight(X, Z);
			Verts[I] = FVector(X, Z, Y); // NOTE: game plane is XY, height on Z
			UVs[I] = FVector2D(Xi / (float)Seg, Zi / (float)Seg);

			FLinearColor C = MeadowC;
			if (X > 40.f) C = FMath::Lerp(MeadowC, RidgeC, FMath::Clamp((X - 40.f) / 18.f, 0.f, 1.f));
			if (Z < -50.f) C = FMath::Lerp(C, LakeC, FMath::Clamp((-Z - 50.f) / 18.f, 0.f, 1.f));
			if (Z > 52.f) C = FMath::Lerp(C, HollowC, FMath::Clamp((Z - 52.f) / 18.f, 0.f, 1.f));
			if (X > 30.f && Z > 60.f) C = DeepC;
			if (Y < 0.35f) C = FMath::Lerp(C, HexColor(TEXT("#E8DCA8")), 0.75f);
			if (InTownPad(X, Z)) C = FMath::Lerp(C, HexColor(TEXT("#C9B98A")), 0.45f);
			if (Y > 7.5f && X > 40.f) C = FMath::Lerp(C, HexColor(TEXT("#9C9284")), FMath::Clamp((Y - 7.5f) / 3.f, 0.f, 1.f));

			const float Checker = ((Xi + Zi) % 2 == 0) ? 1.035f : 0.965f;
			const float Mottle = 1.f + (FMath::PerlinNoise2D(FVector2D(X * 0.35f, Z * 0.35f)) - 0.5f) * 0.12f;
			Colors[I] = C * Checker * Mottle;
			Tangents[I] = FProcMeshTangent(1, 0, 0);
			++I;
		}
	}

	for (int32 Zi = 0; Zi < Seg; ++Zi)
	{
		for (int32 Xi = 0; Xi < Seg; ++Xi)
		{
			const int32 A = Zi * (Seg + 1) + Xi;
			const int32 B = A + 1;
			const int32 C2 = A + Seg + 1;
			const int32 D = C2 + 1;
			Tris.Append({ A, C2, B,  B, C2, D });
		}
	}

	// lift into a fresh actor with collision.
	// Coordinate convention for the whole game: game-space (x, z, height)
	// maps to UE-space (x, -z, height) so the authored layout reads unchanged.
	AActor* TerrainActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(TerrainActor);
	TerrainActor->SetRootComponent(PMC);
	PMC->RegisterComponent();

	TArray<FVector> UeVerts;
	UeVerts.Reserve(Verts.Num());
	for (const FVector& V : Verts) UeVerts.Add(FVector(V.X, -V.Y, V.Z));
	UKismetProceduralMeshLibrary::CalculateTangentsForMesh(UeVerts, Tris, UVs, Normals, Tangents);
	PMC->CreateMeshSection_LinearColor(0, UeVerts, Tris, Normals, UVs, Colors, Tangents, true);
	PMC->ContainsPhysicsTriMeshData(true);
	PMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PMC->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Spawned.Add(TerrainActor);
}

void AEmberwildWorldBuilder::BuildWater()
{
	AActor* Water = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	USceneComponent* Root = NewObject<USceneComponent>(Water);
	Water->SetRootComponent(Root);
	Root->SetWorldLocation(FVector(-8.f, 78.f, 0.45f)); // game z=-78 -> UE y=+78
	Root->RegisterComponent();

	Part(Root, EmberwildPrimPlane(), TEXT("Mirrorlake"), FVector::ZeroVector, FVector(60.f, 50.f, 1.f), TEXT("#4A98C8"));
	Spawned.Add(Water);
}

void AEmberwildWorldBuilder::BuildBridge()
{
	for (int32 i = 0; i < 14; ++i)
	{
		const float T = i / 13.f;
		const float X = FMath::Lerp(BridgeX0, BridgeX1, T);
		const float Zg = BridgeZ + FMath::Sin(T * PI) * 0.5f;
		const float Y = GroundHeight(X, Zg) + 0.2f + FMath::Sin(T * PI) * BridgeArch;

		AActor* Plank = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
		USceneComponent* Root = NewObject<USceneComponent>(Plank);
		Plank->SetRootComponent(Root);
		Root->SetWorldLocation(FVector(X, -Zg, Y)); // game z -> UE -y
		Root->RegisterComponent();
		Part(Root, EmberwildPrimCube(), TEXT("plank"), FVector::ZeroVector, FVector(1.6f, 0.15f, 4.5f),
			i % 2 == 0 ? TEXT("#A4713D") : TEXT("#96653A"));
		Spawned.Add(Plank);
	}
	for (float Side : { -2.1f, 2.1f })
	{
		for (int32 i = 0; i < 7; ++i)
		{
			const float T = i / 6.f;
			const float X = FMath::Lerp(BridgeX0 + 0.2f, BridgeX1 - 0.2f, T);
			const float Zg = BridgeZ + Side;
			const float Y = GroundHeight(X, Zg) + 0.4f + FMath::Sin(T * PI) * BridgeArch;

			AActor* Post = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
			USceneComponent* Root = NewObject<USceneComponent>(Post);
			Post->SetRootComponent(Root);
			Root->SetWorldLocation(FVector(X, -Zg, Y + 0.45f));
			Root->RegisterComponent();
			Part(Root, EmberwildPrimCyl(), TEXT("railpost"), FVector::ZeroVector, FVector(0.14f, 0.45f, 0.14f), TEXT("#7A5230"));
			Spawned.Add(Post);
		}
	}
}

void AEmberwildWorldBuilder::BuildGrass()
{
	// merged tuft field: instanced billboards are overkill for the port; a
	// modest count of small crossed planes reads the same at this camera zoom.
	AActor* Grass = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	USceneComponent* Root = NewObject<USceneComponent>(Grass);
	Grass->SetRootComponent(Root);
	Root->RegisterComponent();

	const int32 Count = 350;
	for (int32 i = 0; i < Count; ++i)
	{
		const float CX = FMath::FRandRange(-88.f, 88.f);
		const float CZ = FMath::FRandRange(-88.f, 88.f);
		const float GY = GroundHeight(CX, CZ);
		if (GY < 0.25f || GY > 7.5f || InTownPad(CX, CZ, 1.f)) continue;

		const float X = CX + FMath::FRandRange(-1.6f, 1.6f);
		const float Z = CZ + FMath::FRandRange(-1.6f, 1.6f);
		const float Y = GroundHeight(X, Z);
		if (Y < 0.25f) continue;

		const float H = FMath::FRandRange(0.35f, 0.75f);
		const FLinearColor C = FMath::Lerp(HexColor(TEXT("#5FB944")), HexColor(TEXT("#B8C84E")), FMath::FRand() * 0.5f);
		const FString Hex = FString::Printf(TEXT("#%02X%02X%02X"),
			FMath::RoundToInt(C.R * 255), FMath::RoundToInt(C.G * 255), FMath::RoundToInt(C.B * 255));

		Part(Root, EmberwildPrimCube(), FString::Printf(TEXT("tuft%d"), i),
			FVector(X, -Z, Y + H / 2), FVector(0.08f, 0.08f, H), Hex,
			FRotator(0.f, FMath::FRandRange(0.f, 180.f), 0.f));
	}
	Spawned.Add(Grass);
}

void AEmberwildWorldBuilder::BuildTown()
{
	BuildPlaza();

	BuildHouse(TEXT("RangerPost"), FVector2D(-14.f, -21.f), 0.f, TEXT("#6E4A2E"), TEXT("#A8503C"));
	BuildHouse(TEXT("ShopHut"),    FVector2D(14.f, -21.f), 0.f, TEXT("#6E4A2E"), TEXT("#3E7A8C"));
	BuildHouse(TEXT("WestHome"),   FVector2D(-19.f, -1.f), 90.f, TEXT("#6E4A2E"), TEXT("#B08A3E"));
	BuildHouse(TEXT("EastHome"),   FVector2D(19.f, -1.f), 90.f, TEXT("#6E4A2E"), TEXT("#5B8C4A"));
	BuildBellTower(FVector2D(0.f, 6.5f));

	BuildFenceRow(FVector(-10.5f, -30.5f, 0), FVector(10.5f, -30.5f, 0), 8);
	BuildFenceRow(FVector(-24.5f, -16.f, 0), FVector(-24.5f, 9.f, 0), 6);
	BuildFenceRow(FVector(24.5f, -16.f, 0), FVector(24.5f, 9.f, 0), 6);
	BuildFenceRow(FVector(-10.5f, 13.5f, 0), FVector(-3.f, 13.5f, 0), 4);
	BuildFenceRow(FVector(3.f, 13.5f, 0), FVector(10.5f, 13.5f, 0), 4);

	SpawnLantern(TEXT("plazaW"), FVector2D(-5.5f, -6.5f));
	SpawnLantern(TEXT("plazaE"), FVector2D(5.5f, -6.5f));
	SpawnLantern(TEXT("north"),  FVector2D(-7.f, 4.f));
	SpawnLantern(TEXT("south"),  FVector2D(9.f, -14.f));

	BuildWell(FVector(-3.4f, -8.4f, 0));
	BuildPathStones(FVector(0.f, -27.f, 0), FVector(0.f, 13.f, 0));
}

AActor* MakeRootedActor(UWorld* World, const FVector& GamePos, float Y)
{
	AActor* A = World->SpawnActor<AActor>(AActor::StaticClass());
	USceneComponent* Root = NewObject<USceneComponent>(A);
	A->SetRootComponent(Root);
	Root->SetWorldLocation(FVector(GamePos.X, -GamePos.Y, Y));
	Root->RegisterComponent();
	return A;
}

void AEmberwildWorldBuilder::BuildPlaza()
{
	constexpr float Half = 5.4f;
	constexpr float Gap = 0.14f;
	AActor* Plaza = MakeRootedActor(GetWorld(), FVector(TownCX, TownCZ, 0), TownY - 0.04f);
	for (int32 Gz = -3; Gz <= 3; ++Gz)
	{
		for (int32 Gx = -3; Gx <= 3; ++Gx)
		{
			const float Px = Gx * (Half / 3.5f);
			const float Pz = Gz * (Half / 3.5f);
			// game offset (Px, Pz): x->UE x, z->UE -y
			Part(Plaza->GetRootComponent(), EmberwildPrimCube(), TEXT("slab"),
				FVector(Px, -Pz, 0), FVector(Half / 3.5f - Gap, Half / 3.5f - Gap, 0.09f), TEXT("#7E786C"),
				FRotator(0.f, FMath::FRandRange(-2.5f, 2.5f), 0.f));
		}
	}
	Spawned.Add(Plaza);
}

void AEmberwildWorldBuilder::BuildHouse(const FString& Name, FVector2D XZ, float RotDeg, const TCHAR* WallHex, const TCHAR* RoofHex)
{
	AActor* House = MakeRootedActor(GetWorld(), FVector(XZ.X, XZ.Y, 0), TownY);
	House->SetActorRotation(FRotator(0.f, -RotDeg, 0.f)); // game CCW->UE CW
	USceneComponent* Root = House->GetRootComponent();

	constexpr float W = 4.6f, D = 3.8f, WallH = 2.3f;
	// game local (lx, lz) -> UE local (lx, -lz, ly)
	Part(Root, EmberwildPrimCube(), TEXT("wallBack"),  FVector(0, D / 2, WallH / 2),      FVector(W, 0.16f, WallH), WallHex);
	Part(Root, EmberwildPrimCube(), TEXT("wallLeft"),  FVector(-W / 2, 0, WallH / 2),    FVector(0.16f, D, WallH), WallHex);
	Part(Root, EmberwildPrimCube(), TEXT("wallRight"), FVector(W / 2, 0, WallH / 2),     FVector(0.16f, D, WallH), WallHex);
	constexpr float DoorW = 1.1f, SideW = (W - DoorW) / 2.f;
	Part(Root, EmberwildPrimCube(), TEXT("wallFrontL"), FVector(-(DoorW + SideW) / 2, -D / 2, WallH / 2), FVector(SideW, 0.16f, WallH), WallHex);
	Part(Root, EmberwildPrimCube(), TEXT("wallFrontR"), FVector((DoorW + SideW) / 2, -D / 2, WallH / 2),  FVector(SideW, 0.16f, WallH), WallHex);
	Part(Root, EmberwildPrimCube(), TEXT("lintel"), FVector(0, -D / 2, WallH - 0.25f), FVector(DoorW + 0.3f, 0.18f, 0.5f), TEXT("#4A3220"));

	constexpr float Slope = 40.f;
	const float Run = W / 2.f + 0.3f;
	const float Len = Run / FMath::Cos(FMath::DegreesToRadians(Slope));
	const float Rise = Run * FMath::Tan(FMath::DegreesToRadians(Slope));
	Part(Root, EmberwildPrimCube(), TEXT("roofL"), FVector(-Run / 2, 0, WallH + Rise / 2), FVector(Len, D + 0.5f, 0.14f), RoofHex, FRotator(0.f, 0.f, -Slope));
	Part(Root, EmberwildPrimCube(), TEXT("roofR"), FVector(Run / 2, 0, WallH + Rise / 2),  FVector(Len, D + 0.5f, 0.14f), RoofHex, FRotator(0.f, 0.f, Slope));
	Part(Root, EmberwildPrimCube(), TEXT("ridgeBeam"), FVector(0, 0, WallH + Rise + 0.06f), FVector(0.22f, D + 0.55f, 0.22f), TEXT("#4A3220"));
	Part(Root, EmberwildPrimCube(), TEXT("door"), FVector(0, -D / 2 - 0.06f, 1.05f), FVector(DoorW - 0.1f, 0.1f, 2.1f), TEXT("#8A5A34"));
	for (float S : { -1.f, 1.f })
		Part(Root, EmberwildPrimCube(), TEXT("window"), FVector(S * (W / 2 + 0.05f), 0.7f, 1.35f), FVector(0.08f, 0.9f, 0.7f), TEXT("#F2E6B8"));
	for (float Sx : { -1.f, 1.f })
		for (float Sz : { -1.f, 1.f })
			Part(Root, EmberwildPrimCube(), TEXT("cornerPost"), FVector(Sx * (W / 2 + 0.02f), -Sz * (D / 2 + 0.02f), WallH / 2), FVector(0.22f, 0.22f, WallH), TEXT("#4A3220"));

	Spawned.Add(House);
}

void AEmberwildWorldBuilder::BuildBellTower(FVector2D XZ)
{
	AActor* Tower = MakeRootedActor(GetWorld(), FVector(XZ.X, XZ.Y, 0), TownY);
	USceneComponent* Root = Tower->GetRootComponent();
	for (float Sx : { -1.f, 1.f })
		for (float Sz : { -1.f, 1.f })
			Part(Root, EmberwildPrimCube(), TEXT("leg"), FVector(Sx * 0.75f, -Sz * 0.75f, 1.55f), FVector(0.22f, 0.22f, 3.1f), TEXT("#6E4A2E"));
	Part(Root, EmberwildPrimCube(), TEXT("roofL"), FVector(-0.62f, 0, 3.45f), FVector(1.5f, 1.9f, 0.12f), TEXT("#A8503C"), FRotator(0.f, 0.f, -38.f));
	Part(Root, EmberwildPrimCube(), TEXT("roofR"), FVector(0.62f, 0, 3.45f),  FVector(1.5f, 1.9f, 0.12f), TEXT("#A8503C"), FRotator(0.f, 0.f, 38.f));
	Part(Root, EmberwildPrimCube(), TEXT("crossbar"), FVector(0, 0, 2.85f), FVector(1.72f, 0.14f, 0.14f), TEXT("#4A3220"));
	Part(Root, EmberwildPrimSphere(), TEXT("bell"), FVector(0, 0, 2.55f), FVector(0.42f, 0.42f, 0.5f), TEXT("#D8B24A"));
	Spawned.Add(Tower);
}

void AEmberwildWorldBuilder::BuildFenceRow(const FVector& From, const FVector& To, int32 Posts)
{
	AActor* Fence = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	USceneComponent* Root = NewObject<USceneComponent>(Fence);
	Fence->SetRootComponent(Root);
	Root->RegisterComponent();
	for (int32 i = 0; i <= Posts; ++i)
	{
		const float T = i / (float)Posts;
		const float X = FMath::Lerp(From.X, To.X, T);
		const float Zg = FMath::Lerp(From.Y, To.Y, T);
		Part(Root, EmberwildPrimCube(), TEXT("post"), FVector(X, -Zg, TownY + 0.5f), FVector(0.14f, 0.14f, 1.f), TEXT("#7A5A38"));
		if (i < Posts)
		{
			const float T2 = (i + 1) / (float)Posts;
			const float NX = FMath::Lerp(From.X, To.X, T2);
			const float NZ = FMath::Lerp(From.Y, To.Y, T2);
			Part(Root, EmberwildPrimCube(), TEXT("rail"),
				FVector((X + NX) / 2, -(Zg + NZ) / 2, TownY + 0.78f),
				FVector(FMath::Abs(NX - X) + 0.18f, FMath::Abs(NZ - Zg) + 0.18f, 0.1f), TEXT("#7A5A38"));
		}
	}
	Spawned.Add(Fence);
}

void AEmberwildWorldBuilder::SpawnLantern(const FString& Name, FVector2D XZ)
{
	AActor* Lamp = MakeRootedActor(GetWorld(), FVector(XZ.X, XZ.Y, 0), TownY);
	USceneComponent* Root = Lamp->GetRootComponent();
	Part(Root, EmberwildPrimCube(), TEXT("post"), FVector(0, 0, 1.05f), FVector(0.1f, 0.1f, 2.1f), TEXT("#3A3A42"));
	Part(Root, EmberwildPrimCube(), TEXT("glass"), FVector(0, 0, 2.2f), FVector(0.3f, 0.3f, 0.34f), TEXT("#FFD75E"));
	Part(Root, EmberwildPrimCube(), TEXT("cap"), FVector(0, 0, 2.42f), FVector(0.4f, 0.4f, 0.08f), TEXT("#3A3A42"));
	Spawned.Add(Lamp);
}

void AEmberwildWorldBuilder::BuildWell(const FVector& At)
{
	AActor* Well = MakeRootedActor(GetWorld(), FVector(At.X, At.Y, 0), TownY);
	USceneComponent* Root = Well->GetRootComponent();
	for (int32 i = 0; i < 8; ++i)
	{
		const float A = i / 8.f * 2.f * PI;
		Part(Root, EmberwildPrimCube(), TEXT("stone"),
			FVector(FMath::Cos(A) * 0.75f, -FMath::Sin(A) * 0.75f, 0.3f),
			FVector(0.52f, 0.3f, 0.6f), TEXT("#8E8A80"), FRotator(0.f, -FMath::RadiansToDegrees(A), 0.f));
	}
	Part(Root, EmberwildPrimCyl(), TEXT("water"), FVector(0, 0, 0.32f), FVector(0.6f, 0.6f, 0.02f), TEXT("#3E6A8C"));
	for (float S : { -1.f, 1.f })
		Part(Root, EmberwildPrimCube(), TEXT("wellPost"), FVector(S * 0.7f, 0, 1.3f), FVector(0.12f, 0.12f, 1.6f), TEXT("#6E4A2E"));
	Part(Root, EmberwildPrimCube(), TEXT("beam"), FVector(0, 0, 2.1f), FVector(1.6f, 0.1f, 0.1f), TEXT("#6E4A2E"));
	Part(Root, EmberwildPrimCube(), TEXT("roofL"), FVector(-0.5f, 0, 2.4f), FVector(1.2f, 1.2f, 0.08f), TEXT("#A8503C"), FRotator(0.f, 0.f, -35.f));
	Part(Root, EmberwildPrimCube(), TEXT("roofR"), FVector(0.5f, 0, 2.4f),  FVector(1.2f, 1.2f, 0.08f), TEXT("#A8503C"), FRotator(0.f, 0.f, 35.f));
	Spawned.Add(Well);
}

void AEmberwildWorldBuilder::BuildPathStones(const FVector& From, const FVector& To)
{
	AActor* Path = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	USceneComponent* Root = NewObject<USceneComponent>(Path);
	Path->SetRootComponent(Root);
	Root->RegisterComponent();
	constexpr int32 N = 9;
	for (int32 i = 0; i <= N; ++i)
	{
		const float T = i / (float)N;
		const float X = FMath::Lerp(From.X, To.X, T) + FMath::Sin(i * 2.7f) * 0.7f;
		const float Zg = FMath::Lerp(From.Y, To.Y, T);
		Part(Root, EmberwildPrimCube(), TEXT("stone"),
			FVector(X, -Zg, TownY - 0.02f),
			FVector(FMath::FRandRange(0.8f, 1.2f), FMath::FRandRange(0.6f, 0.9f), 0.06f), TEXT("#847D6E"),
			FRotator(0.f, FMath::FRandRange(-12.f, 12.f), 0.f));
	}
	Spawned.Add(Path);
}

void AEmberwildWorldBuilder::ScatterTrees(const TCHAR* Zone, int32 Count, FVector2D XRange, FVector2D ZRange, const TCHAR* LeafHex)
{
	AActor* Trees = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	USceneComponent* Root = NewObject<USceneComponent>(Trees);
	Trees->SetRootComponent(Root);
	Root->RegisterComponent();

	for (int32 i = 0; i < Count; ++i)
	{
		const float PX = FMath::FRandRange(XRange.X, XRange.Y);
		const float PZ = FMath::FRandRange(ZRange.X, ZRange.Y);
		if (InTownPad(PX, PZ, 1.f)) continue;
		const float GY = GroundHeight(PX, PZ);
		if (GY < 0.2f) continue;

		const float TrunkH = FMath::FRandRange(1.6f, 2.4f);
		const FString TreeName = FString::Printf(TEXT("tree_%s_%d"), Zone, i);
		Part(Root, EmberwildPrimCyl(), TreeName + TEXT("_trunk"), FVector(PX, -PZ, GY + TrunkH / 2),
			FVector(0.22f, 0.22f, TrunkH), TEXT("#6E4A2E"));

		const float CrownY = GY + TrunkH + 0.5f;
		Part(Root, EmberwildPrimSphere(), TreeName + TEXT("_crown"), FVector(PX, -PZ, CrownY),
			FVector(1.5f, 1.5f, 1.3f), LeafHex);
		Part(Root, EmberwildPrimSphere(), TreeName + TEXT("_crown2"), FVector(PX + 0.4f, -PZ + 0.3f, CrownY - 0.4f),
			FVector(1.0f, 1.0f, 0.9f), LeafHex);
	}
	Spawned.Add(Trees);
}

void AEmberwildWorldBuilder::ScatterRocks(const TCHAR* Zone, int32 Count, FVector2D XRange, FVector2D ZRange)
{
	AActor* Rocks = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	USceneComponent* Root = NewObject<USceneComponent>(Rocks);
	Rocks->SetRootComponent(Root);
	Root->RegisterComponent();
	for (int32 i = 0; i < Count; ++i)
	{
		const float PX = FMath::FRandRange(XRange.X, XRange.Y);
		const float PZ = FMath::FRandRange(ZRange.X, ZRange.Y);
		if (InTownPad(PX, PZ)) continue;
		const float GY = GroundHeight(PX, PZ);
		if (GY < 0.2f) continue;
		const float RS = FMath::FRandRange(0.3f, 0.9f);
		Part(Root, EmberwildPrimCube(), FString::Printf(TEXT("rock_%s_%d"), Zone, i),
			FVector(PX, -PZ, GY + RS * 0.3f), FVector(RS, RS * FMath::FRandRange(0.7f, 1.2f), RS * 0.7f), TEXT("#8E8A80"),
			FRotator(FMath::FRandRange(0.f, 20.f), FMath::FRandRange(0.f, 90.f), 0.f));
	}
	Spawned.Add(Rocks);
}

// Gathering nodes, stations, shop, slots machines, melee targets and NPCs are
// spawned by UEmberwildSubsystem::BootWorld via the EmberwildSpawn* helpers
// (see EmberwildGame.cpp) so content lives in one orchestration place.

void AEmberwildWorldBuilder::Tick(float Dt)
{
	// gentle crown/flame sway, mirroring the original's idle animation
	Elapsed += Dt;
	for (FSWayEntry& E : SwayEntries)
	{
		if (!E.Comp.IsValid()) continue;
		if (E.bFlame)
		{
			const FVector Scale = E.Comp->GetRelativeScale3D();
			const float Ph = Elapsed * 9.f + E.Phase;
			E.Comp->SetRelativeScale3D(FVector(Scale.X, Scale.Y, FMath::Max(0.05f, Scale.Z * (1.f + FMath::Sin(Ph) * 0.001f))));
		}
		else
		{
			const float Ph = Elapsed * 1.4f + E.Phase;
			E.Comp->SetRelativeRotation(FRotator(FMath::Sin(Ph) * 2.f, FMath::Sin(Ph * 0.6f) * 4.f, FMath::Cos(Ph * 0.8f) * 1.5f));
		}
	}
}

// ================================================================ interactables
AInteractableActor::AInteractableActor()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	USphereComponent* Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->InitSphereRadius(2.2f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

FString AInteractableActor::Prompt() const
{
	switch (Kind)
	{
	case EInteractableKind::Npc:
		if (const FNpcDef* D = FEmberwildDB::Npc(RefId))
			return FString::Printf(TEXT("%s  [talk]"), *D->Name);
		return TEXT("Talk");
	case EInteractableKind::GatherNode:
	{
		const FNodeDef* D = FEmberwildDB::Node(RefId);
		if (!D) return TEXT("?");
		if (bDepleted) return FString::Printf(TEXT("%s (depleted)"), *D->Name);
		if (UGameInstance* GI = GetGameInstance())
			if (auto* State = GI->GetSubsystem<UEmberwildSubsystem>())
				if (State->Save.ProfLevel(D->Profession) < D->RequiredLevel)
					return FString::Printf(TEXT("%s (needs %s Lv%d)"), *D->Name, FProfessionSystem::Name(D->Profession), D->RequiredLevel);
		return FString::Printf(TEXT("%s  [%s]"), *D->Name, FProfessionSystem::Icon(D->Profession));
	}
	case EInteractableKind::CraftStation: return RefId + TEXT("  [craft]");
	case EInteractableKind::ShopStall:    return TEXT("Vale Post Traders  [shop]");
	case EInteractableKind::SlotsMachine: return TEXT("Lucky Snorlax  [slots]");
	case EInteractableKind::MeleeTarget:
	{
		const FSpeciesDef* D = FEmberwildDB::SpeciesDef(RefId);
		return FString::Printf(TEXT("%s (Lv%d)  [attack]"), D ? *D->Name : *RefId, SpawnLevel);
	}
	}
	return TEXT("");
}

void AInteractableActor::Interact(AEmberwildPlayer* Player)
{
	if (UGameInstance* GI = GetGameInstance())
		if (auto* Sub = GI->GetSubsystem<UEmberwildSubsystem>())
			Sub->HandleInteract(this, Player);
}

void AInteractableActor::BuildVisual(AEmberwildWorldBuilder* World)
{
	switch (Kind)
	{
	case EInteractableKind::Npc:
		if (const FNpcDef* D = FEmberwildDB::Npc(RefId)) BuildNpcVisual(*D);
		break;
	case EInteractableKind::GatherNode:
		if (const FNodeDef* D = FEmberwildDB::Node(RefId)) BuildNodeVisual(*D);
		break;
	case EInteractableKind::CraftStation: BuildStationVisual(RefId); break;
	case EInteractableKind::ShopStall:    BuildShopVisual(); break;
	case EInteractableKind::SlotsMachine: BuildSlotsVisual(); break;
	case EInteractableKind::MeleeTarget:  break; // the creature actor builds its own body
	}
}

void AInteractableActor::BuildNpcVisual(const FNpcDef& Def)
{
	USceneComponent* Root = GetRootComponent();
	// body
	Part(Root, EmberwildPrimCyl(), TEXT("body"), FVector(0, 0, 0.85f), FVector(0.62f, 0.62f, 1.6f), Def.ColorHex);
	Part(Root, EmberwildPrimCyl(), TEXT("belt"), FVector(0, 0, 0.78f), FVector(0.66f, 0.66f, 0.12f), TEXT("#5A4632"));
	// head + hair
	Part(Root, EmberwildPrimSphere(), TEXT("head"), FVector(0, 0, 1.82f), FVector(0.55f, 0.5f, 0.52f), TEXT("#F2CFA0"));
	Part(Root, EmberwildPrimSphere(), TEXT("hair"), FVector(0, 0.03f, 1.92f), FVector(0.58f, 0.54f, 0.42f), TEXT("#4A3626"));
	// eyes (facing -Y in UE = +Z game "south" toward camera at spawn)
	Part(Root, EmberwildPrimSphere(), TEXT("eyeL"), FVector(-0.12f, -0.21f, 1.84f), FVector(0.09f, 0.06f, 0.13f), TEXT("#2A3438"));
	Part(Root, EmberwildPrimSphere(), TEXT("eyeR"), FVector(0.12f, -0.21f, 1.84f),  FVector(0.09f, 0.06f, 0.13f), TEXT("#2A3438"));
}

void AInteractableActor::BuildNodeVisual(const FNodeDef& Def)
{
	USceneComponent* Root = GetRootComponent();
	switch (Def.Profession)
	{
	case EProfessionId::Forager:
	{
		FString Leaf = TEXT("#58A852");
		if (RefId.Contains(TEXT("fiber"))) Leaf = TEXT("#6FA84E");
		else if (RefId.Contains(TEXT("pepper"))) Leaf = TEXT("#4E8F42");
		Part(Root, EmberwildPrimSphere(), TEXT("bush"), FVector(0, 0, 0.45f), FVector(0.95f, 0.95f, 0.8f), Leaf);
		Part(Root, EmberwildPrimSphere(), TEXT("bush2"), FVector(0.42f, -0.2f, 0.35f), FVector(0.6f, 0.6f, 0.55f), TEXT("#6FBF5E"));
		const FString Fruit = RefId.Contains(TEXT("pepper")) ? TEXT("#E85030") : RefId.Contains(TEXT("fiber")) ? TEXT("#D8C860") : TEXT("#E87088");
		Part(Root, EmberwildPrimSphere(), TEXT("fruit1"), FVector(0.3f, -0.35f, 0.62f), FVector(0.14f, 0.14f, 0.14f), Fruit);
		Part(Root, EmberwildPrimSphere(), TEXT("fruit2"), FVector(-0.28f, -0.4f, 0.5f), FVector(0.12f, 0.12f, 0.12f), Fruit);
		break;
	}
	case EProfessionId::Miner:
	{
		Part(Root, EmberwildPrimCube(), TEXT("rock"), FVector(0, 0, 0.42f), FVector(1.f, 0.95f, 0.8f), TEXT("#8E8A80"), FRotator(0.f, 30.f, 0.f));
		Part(Root, EmberwildPrimCube(), TEXT("rock2"), FVector(0.35f, 0.3f, 0.3f), FVector(0.5f, 0.5f, 0.5f), TEXT("#7E7A72"), FRotator(0.f, -20.f, 12.f));
		const FString Ore = RefId.Contains(TEXT("copper")) ? TEXT("#C87838") : RefId.Contains(TEXT("iron")) ? TEXT("#B8BCC4")
			: RefId.Contains(TEXT("moon")) ? TEXT("#9AB8E8") : TEXT("#C8BC98");
		Part(Root, EmberwildPrimSphere(), TEXT("ore1"), FVector(0.2f, -0.25f, 0.72f), FVector(0.16f, 0.14f, 0.12f), Ore);
		Part(Root, EmberwildPrimSphere(), TEXT("ore2"), FVector(-0.3f, 0.2f, 0.55f), FVector(0.13f, 0.12f, 0.1f), Ore);
		break;
	}
	default: // fishing spot
		Part(Root, EmberwildPrimCyl(), TEXT("water"), FVector(0, 0, 0.02f), FVector(1.7f, 1.7f, 0.02f), TEXT("#3E7CA8"));
		Part(Root, EmberwildPrimCyl(), TEXT("water2"), FVector(0, 0, 0.04f), FVector(1.1f, 1.1f, 0.015f), TEXT("#5FA0CC"));
		Part(Root, EmberwildPrimSphere(), TEXT("bobber"), FVector(0.15f, -0.1f, 0.14f), FVector(0.16f, 0.16f, 0.16f), TEXT("#E85848"));
		break;
	}
}

void AInteractableActor::BuildStationVisual(const FString& StationId)
{
	USceneComponent* Root = GetRootComponent();
	if (StationId == TEXT("campfire"))
	{
		for (int32 i = 0; i < 7; ++i)
		{
			const float A = i / 7.f * 2.f * PI;
			Part(Root, EmberwildPrimSphere(), TEXT("stone"), FVector(FMath::Cos(A) * 0.7f, -FMath::Sin(A) * 0.7f, 0.12f),
				FVector(0.3f, 0.3f, 0.24f), TEXT("#6E6A62"));
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const float A = i / 4.f * 2.f * PI;
			Part(Root, EmberwildPrimCyl(), TEXT("log"), FVector(FMath::Cos(A) * 0.15f, -FMath::Sin(A) * 0.15f, 0.16f),
				FVector(0.1f, 0.1f, 0.8f), TEXT("#5E4326"), FRotator(0.f, FMath::RadiansToDegrees(A) + 45.f, 90.f));
		}
		Part(Root, EmberwildPrimSphere(), TEXT("flame"), FVector(0, 0, 0.6f), FVector(0.45f, 0.45f, 0.6f), TEXT("#FF9A3C"));
		Part(Root, EmberwildPrimSphere(), TEXT("flame2"), FVector(0, 0, 0.9f), FVector(0.28f, 0.28f, 0.4f), TEXT("#FFD75E"));
	}
	else if (StationId == TEXT("forge"))
	{
		Part(Root, EmberwildPrimCube(), TEXT("anvil"), FVector(0, 0, 0.55f), FVector(0.7f, 0.4f, 0.5f), TEXT("#4A4A52"));
		Part(Root, EmberwildPrimCube(), TEXT("base"), FVector(0, 0, 0.2f), FVector(0.9f, 0.6f, 0.4f), TEXT("#5A4A42"));
		Part(Root, EmberwildPrimCyl(), TEXT("chimney"), FVector(0.45f, 0.3f, 1.2f), FVector(0.3f, 0.3f, 1.6f), TEXT("#6A5A50"));
		Part(Root, EmberwildPrimSphere(), TEXT("ember"), FVector(0, 0, 0.86f), FVector(0.3f, 0.3f, 0.2f), TEXT("#FF7A3C"));
	}
	else // workbench
	{
		Part(Root, EmberwildPrimCube(), TEXT("top"), FVector(0, 0, 0.9f), FVector(1.8f, 0.9f, 0.1f), TEXT("#8A6A44"));
		for (float Sx : { -0.75f, 0.75f })
			for (float Sy : { -0.3f, 0.3f })
				Part(Root, EmberwildPrimCube(), TEXT("leg"), FVector(Sx, Sy, 0.45f), FVector(0.12f, 0.12f, 0.9f), TEXT("#6E4A2E"));
		Part(Root, EmberwildPrimCube(), TEXT("vice"), FVector(0.6f, 0, 1.0f), FVector(0.2f, 0.3f, 0.2f), TEXT("#5A5A62"));
	}
}

void AInteractableActor::BuildShopVisual()
{
	// market stall: counter + awning + posts
	USceneComponent* Root = GetRootComponent();
	Part(Root, EmberwildPrimCube(), TEXT("counter"), FVector(0, 0, 0.55f), FVector(1.8f, 0.7f, 1.1f), TEXT("#8A6A44"));
	Part(Root, EmberwildPrimCube(), TEXT("awning"), FVector(0, -0.4f, 2.1f), FVector(2.1f, 1.2f, 0.1f), TEXT("#A8503C"), FRotator(-12.f, 0.f, 0.f));
	for (float Sx : { -0.95f, 0.95f })
		Part(Root, EmberwildPrimCube(), TEXT("post"), FVector(Sx, 0.3f, 1.05f), FVector(0.1f, 0.1f, 2.1f), TEXT("#6E4A2E"));
	Part(Root, EmberwildPrimSphere(), TEXT("goods1"), FVector(-0.5f, 0, 1.2f), FVector(0.25f, 0.25f, 0.25f), TEXT("#D9A05B"));
	Part(Root, EmberwildPrimSphere(), TEXT("goods2"), FVector(0.2f, 0.1f, 1.2f), FVector(0.2f, 0.2f, 0.2f), TEXT("#7C9A4E"));
}

void AInteractableActor::BuildSlotsVisual()
{
	// Lucky Snorlax cabinet: body, screen, side lever, coin tray
	USceneComponent* Root = GetRootComponent();
	Part(Root, EmberwildPrimCube(), TEXT("cabinet"), FVector(0, 0, 1.1f), FVector(1.f, 0.7f, 2.2f), TEXT("#7A4A9E"));
	Part(Root, EmberwildPrimCube(), TEXT("screen"), FVector(0, -0.36f, 1.5f), FVector(0.7f, 0.05f, 0.5f), TEXT("#2A2240"));
	Part(Root, EmberwildPrimSphere(), TEXT("screenReel"), FVector(0, -0.4f, 1.5f), FVector(0.12f, 0.05f, 0.12f), TEXT("#F8D030"));
	Part(Root, EmberwildPrimCyl(), TEXT("lever"), FVector(0.55f, 0, 1.6f), FVector(0.05f, 0.05f, 0.5f), TEXT("#B8BCC4"), FRotator(15.f, 0.f, 0.f));
	Part(Root, EmberwildPrimSphere(), TEXT("leverBall"), FVector(0.55f, -0.1f, 1.85f), FVector(0.12f, 0.12f, 0.12f), TEXT("#E85030"));
	Part(Root, EmberwildPrimCube(), TEXT("tray"), FVector(0, -0.4f, 0.6f), FVector(0.5f, 0.2f, 0.1f), TEXT("#4A3220"));
}

// ================================================================ creature actor
ACreatureActor::ACreatureActor()
{
	PrimaryActorTick.bCanEverTick = true;
	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	SetRootComponent(BodyRoot);
}

ACreatureActor* ACreatureActor::Spawn(const FCreature& Creature, FVector At, bool bAsMeleeTarget)
{
	UWorld* W = GEngine && GEngine->GetWorldContexts().Num() > 0
		? GEngine->GetWorldContexts()[0].World() : nullptr;
	if (!W) return nullptr;

	FActorSpawnParameters Params;
	ACreatureActor* A = W->SpawnActor<ACreatureActor>(Params);
	if (!A) return nullptr;
	A->SpeciesId = Creature.SpeciesId;
	A->Level = Creature.Level;
	A->bIsMeleeTarget = bAsMeleeTarget;
	A->bShiny = Creature.bShiny;
	A->HitPoints = FMath::Max(6, Creature.MaxHP() / 4);
	A->SetActorLocation(At);
	A->BuildBody();
	return A;
}

void ACreatureActor::BuildBody()
{
	const FSpeciesDef* Sp = FEmberwildDB::SpeciesDef(SpeciesId);
	if (!Sp) return;

	const FLinearColor Body = AEmberwildWorldBuilder::HexColor(Sp->PaletteHex);
	const FLinearColor Accent = AEmberwildWorldBuilder::HexColor(Sp->AccentHex);
	auto HexOf = [](const FLinearColor& C) {
		return FString::Printf(TEXT("#%02X%02X%02X"),
			FMath::RoundToInt(C.R * 255), FMath::RoundToInt(C.G * 255), FMath::RoundToInt(C.B * 255));
	};
	const float S = Sp->Scale;

	// simple legless chibi quadruped/bird/fish body: egg body + head + ears,
	// tinted by species palette. Shapes differ per family like the original.
	Part(BodyRoot, EmberwildPrimSphere(), TEXT("torso"), FVector(0, 0, 0.55f * S),
		FVector(0.6f * S, 0.55f * S, 0.5f * S), HexOf(Body));
	Part(BodyRoot, EmberwildPrimSphere(), TEXT("head"), FVector(0, -0.25f * S, 0.95f * S),
		FVector(0.42f * S, 0.4f * S, 0.4f * S), HexOf(Body));
	Part(BodyRoot, EmberwildPrimSphere(), TEXT("snout"), FVector(0, -0.5f * S, 0.88f * S),
		FVector(0.2f * S, 0.18f * S, 0.16f * S), HexOf(Accent));

	// ears / fins per shape
	if (Sp->Shape == TEXT("quad") || Sp->Shape == TEXT("beast"))
	{
		Part(BodyRoot, EmberwildPrimCyl(), TEXT("earL"),
			FVector(-0.16f * S, -0.2f * S, 1.25f * S), FVector(0.12f * S, 0.12f * S, 0.3f * S), HexOf(Accent));
		Part(BodyRoot, EmberwildPrimCyl(), TEXT("earR"),
			FVector(0.16f * S, -0.2f * S, 1.25f * S), FVector(0.12f * S, 0.12f * S, 0.3f * S), HexOf(Accent));
		Part(BodyRoot, EmberwildPrimCyl(), TEXT("tail"),
			FVector(0, 0.45f * S, 0.7f * S), FVector(0.1f * S, 0.1f * S, 0.4f * S), HexOf(Accent), FRotator(35.f, 0.f, 0.f));
	}
	else if (Sp->Shape == TEXT("avian"))
	{
		Part(BodyRoot, EmberwildPrimCyl(), TEXT("beak"), FVector(0, -0.55f * S, 0.95f * S),
			FVector(0.07f * S, 0.07f * S, 0.18f * S), HexOf(Accent), FRotator(90.f, 0.f, 0.f));
		Part(BodyRoot, EmberwildPrimCube(), TEXT("wingL"), FVector(-0.5f * S, 0, 0.6f * S),
			FVector(0.08f * S, 0.35f * S, 0.3f * S), HexOf(Accent));
		Part(BodyRoot, EmberwildPrimCube(), TEXT("wingR"), FVector(0.5f * S, 0, 0.6f * S),
			FVector(0.08f * S, 0.35f * S, 0.3f * S), HexOf(Accent));
	}
	else if (Sp->Shape == TEXT("aquatic"))
	{
		Part(BodyRoot, EmberwildPrimCyl(), TEXT("finTop"), FVector(0, 0, 0.95f * S),
			FVector(0.06f * S, 0.25f * S, 0.3f * S), HexOf(Accent));
		Part(BodyRoot, EmberwildPrimCube(), TEXT("finL"), FVector(-0.5f * S, 0, 0.5f * S),
			FVector(0.06f * S, 0.3f * S, 0.25f * S), HexOf(Accent));
		Part(BodyRoot, EmberwildPrimCube(), TEXT("finR"), FVector(0.5f * S, 0, 0.5f * S),
			FVector(0.06f * S, 0.3f * S, 0.25f * S), HexOf(Accent));
	}
	else // rock, spirit, bug: crystal/floating/mushroom silhouettes
	{
		Part(BodyRoot, EmberwildPrimCyl(), TEXT("crest"), FVector(0, 0, 1.0f * S),
			FVector(0.15f * S, 0.15f * S, 0.35f * S), HexOf(Accent));
	}

	// eyes
	Part(BodyRoot, EmberwildPrimSphere(), TEXT("eyeL"), FVector(-0.12f * S, -0.45f * S, 1.0f * S),
		FVector(0.07f * S, 0.05f * S, 0.1f * S), TEXT("#2A3438"));
	Part(BodyRoot, EmberwildPrimSphere(), TEXT("eyeR"), FVector(0.12f * S, -0.45f * S, 1.0f * S),
		FVector(0.07f * S, 0.05f * S, 0.1f * S), TEXT("#2A3438"));

	// shiny: golden sparkle ring
	if (bShiny)
		Part(BodyRoot, EmberwildPrimCyl(), TEXT("shinyRing"), FVector(0, 0, 0.05f),
			FVector(1.1f * S, 1.1f * S, 0.02f), TEXT("#F2CC58"));

	BaseZ = GetActorLocation().Z;
	BobPhase = FMath::FRand() * 2.f * PI;
}

int32 ACreatureActor::TakeHit(int32 Damage)
{
	if (bDefeated) return 0;
	HitPoints -= Damage;
	return HitPoints;
}

void AInteractableActor::Tick(float Dt)
{
	// gather-node respawn bookkeeping
	if (Kind == EInteractableKind::GatherNode && bDepleted && RespawnAt > 0.f)
		if (auto* Sub = Emberwild())
			if (Sub->PlaySeconds() >= RespawnAt)
			{
				bDepleted = false;
				RespawnAt = -1.f;
				SetActorHiddenInGame(false);
			}
}

void ACreatureActor::Tick(float Dt)
{
	// idle bob
	BobPhase += Dt * 2.2f;
	const FVector Loc = GetActorLocation();
	SetActorLocation(FVector(Loc.X, Loc.Y, BaseZ + FMath::Sin(BobPhase) * 0.08f));
}

// ================================================================ player pawn
AEmberwildPlayer::AEmberwildPlayer()
{
	PrimaryActorTick.bCanEverTick = true;

	// gentle sphere collision so the pawn slides along terrain & props
	USphereComponent* Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(0.5f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SetRootComponent(Collision);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 16.f;
	SpringArm->SetRelativeRotation(FRotator(-38.f, -90.f, 0.f));
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->SetFieldOfView(55.f);

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 7.f;
	Movement->Acceleration = 60.f;
	Movement->Deceleration = 80.f;
}

void AEmberwildPlayer::BuildBody()
{
	if (BodyBuilt) return;
	BodyBuilt = true;

	// chibi capsule body: tunic + head + cap + backpack, like the fallback body
	Part(GetRootComponent(), EmberwildPrimCyl(), TEXT("tunic"), FVector(0, 0, 0.75f),
		FVector(0.55f, 0.55f, 1.5f), TEXT("#E0784A"));
	Part(GetRootComponent(), EmberwildPrimSphere(), TEXT("head"), FVector(0, 0, 1.6f),
		FVector(0.42f, 0.42f, 0.42f), TEXT("#F2CFA0"));
	Part(GetRootComponent(), EmberwildPrimSphere(), TEXT("cap"), FVector(0, 0.02f, 1.74f),
		FVector(0.44f, 0.44f, 0.26f), TEXT("#D8604A"));
	Part(GetRootComponent(), EmberwildPrimCube(), TEXT("pack"), FVector(0, 0.4f, 0.95f),
		FVector(0.32f, 0.2f, 0.4f), TEXT("#8A5E33"));
	Part(GetRootComponent(), EmberwildPrimSphere(), TEXT("eyeL"), FVector(-0.13f, -0.3f, 1.62f),
		FVector(0.09f, 0.06f, 0.13f), TEXT("#2A3438"));
	Part(GetRootComponent(), EmberwildPrimSphere(), TEXT("eyeR"), FVector(0.13f, -0.3f, 1.62f),
		FVector(0.09f, 0.06f, 0.13f), TEXT("#2A3438"));
}

void AEmberwildPlayer::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Sub = Emberwild()) Sub->Player = this;
	BuildBody();
}

void AEmberwildPlayer::FaceDirection(const FVector& Dir)
{
	// the body's face sits on local -Y; yaw it so -Y points along Dir
	if (Dir.SizeSquared2D() > KINDA_SMALL_NUMBER)
		SetActorRotation(FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Dir.X, -Dir.Y)), 0.f));
}

void AEmberwildPlayer::Tick(float Dt)
{
	Super::Tick(Dt);

	// camera zoom easing
	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArm, Dt, 8.f);

	// consume accumulated axis input
	if (AccumMove.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		if (auto* Sub2 = Emberwild())
			if (Sub2->UIRouter.IsValid() && !Sub2->UIRouter->IsWorldInputAllowed())
				AccumMove = FVector2D::ZeroVector;
		MoveAxis(AccumMove);
	}
	AccumMove = FVector2D::ZeroVector;

	// stick to the walkable surface (terrain + bridge deck)
	const FVector Loc = GetActorLocation();
	const float Ground = AEmberwildWorldBuilder::SurfaceHeight(Loc.X, -Loc.Y);
	SetActorLocation(FVector(Loc.X, Loc.Y, FMath::Max(Loc.Z, Ground + 0.6f)));

	// melee swing timer
	if (SwingTimer >= 0.f)
	{
		SwingTimer -= Dt;
		if (SwingTimer < 0.f && WeaponPivot) WeaponPivot->SetRelativeRotation(FRotator::ZeroRotator);
	}
	if (CooldownTimer > 0.f) CooldownTimer -= Dt;

	// encounters & autosave tick here (single-player pawn is always alive)
	if (auto* Sub = Emberwild())
	{
		Sub->RollEncounter(Dt);

		// zone tracking from position
		for (const TPair<FString, FVector2D>& KV : AEmberwildWorldBuilder::ZoneCenters())
		{
			const FVector2D C = KV.Value;
			const float Dx = Loc.X - C.X;
			const float Dy = -Loc.Y - C.Y;
			if (KV.Key == TEXT("meadow") ? (FMath::Abs(Dx) < 36.f && FMath::Abs(Dy) < 36.f)
				: (Dx * Dx + Dy * Dy < 24.f * 24.f))
			{
				Sub->SetZone(KV.Key);
				break;
			}
		}

		// autosave every 25s
		Sub->NextAutosave -= Dt;
		if (Sub->NextAutosave <= 0.f)
		{
			Sub->NextAutosave = 25.f;
			Sub->Save.PlayerX = Loc.X;
			Sub->Save.PlayerZ = -Loc.Y;
			FEmberwildSave::Save(Sub->Save);
		}

		Sub->SpawnSleeper();
	}
}

void AEmberwildPlayer::MoveAxis(const FVector2D& Axis)
{
	MoveInput = Axis;
	if (Axis.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		// camera-relative: camera looks down -X rotated by its yaw
		const FRotator CamRot = Camera->GetComponentRotation();
		const FVector Fwd = FVector(CamRot.Vector().X, CamRot.Vector().Y, 0.f).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
		const FVector Dir = Fwd * Axis.Y + Right * Axis.X;
		Movement->AddInputVector(Dir.GetSafeNormal());
		FaceDirection(Dir);
	}
}

void AEmberwildPlayer::Zoom(float Delta)
{
	TargetArm = FMath::Clamp(TargetArm + Delta * 2.f, 8.f, 28.f);
}

void AEmberwildPlayer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	InputComponent->BindAxis(TEXT("MoveForward"), this, &AEmberwildPlayer::OnMoveForward);
	InputComponent->BindAxis(TEXT("MoveRight"), this, &AEmberwildPlayer::OnMoveRight);
	InputComponent->BindAxis(TEXT("Zoom"), this, &AEmberwildPlayer::OnZoomAxis);

	InputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &AEmberwildPlayer::OnInteract);
	InputComponent->BindAction(TEXT("Swing"), IE_Pressed, this, &AEmberwildPlayer::OnSwing);
	InputComponent->BindAction(TEXT("Bag"), IE_Pressed, this, &AEmberwildPlayer::OnBag);
	InputComponent->BindAction(TEXT("Gear"), IE_Pressed, this, &AEmberwildPlayer::OnGear);
	InputComponent->BindAction(TEXT("Journal"), IE_Pressed, this, &AEmberwildPlayer::OnJournal);
	InputComponent->BindAction(TEXT("Creatures"), IE_Pressed, this, &AEmberwildPlayer::OnCreatures);
	InputComponent->BindAction(TEXT("Weapons"), IE_Pressed, this, &AEmberwildPlayer::OnWeapons);
	InputComponent->BindAction(TEXT("Back"), IE_Pressed, this, &AEmberwildPlayer::OnBack);

	InputComponent->BindKey(EKeys::Up, IE_Pressed, this, &AEmberwildPlayer::OnUIUp);
	InputComponent->BindKey(EKeys::Down, IE_Pressed, this, &AEmberwildPlayer::OnUIDown);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AEmberwildPlayer::OnUIEnter);
	InputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &AEmberwildPlayer::OnUIBackspace);
	InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AEmberwildPlayer::OnUIY);
	InputComponent->BindKey(EKeys::N, IE_Pressed, this, &AEmberwildPlayer::OnUIN);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &AEmberwildPlayer::OnUIB);
	InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AEmberwildPlayer::OnUIF5);
}

void AEmberwildPlayer::OnUIUp()       { OnUIKey(EKeys::Up); }
void AEmberwildPlayer::OnUIDown()     { OnUIKey(EKeys::Down); }
void AEmberwildPlayer::OnUIEnter()    { OnUIKey(EKeys::Enter); }
void AEmberwildPlayer::OnUIBackspace(){ OnUIKey(EKeys::BackSpace); }
void AEmberwildPlayer::OnUIY()        { OnUIKey(EKeys::Y); }
void AEmberwildPlayer::OnUIN()        { OnUIKey(EKeys::N); }
void AEmberwildPlayer::OnUIB()        { OnUIKey(EKeys::B); }
void AEmberwildPlayer::OnUIF5()       { OnUIKey(EKeys::F5); }

void AEmberwildPlayer::OnMoveForward(float V) { AccumMove.Y += V; }
void AEmberwildPlayer::OnMoveRight(float V)   { AccumMove.X += V; }
void AEmberwildPlayer::OnZoomAxis(float V)    { Zoom(V); }
void AEmberwildPlayer::OnInteract()           { InteractPressed(); }
void AEmberwildPlayer::OnSwing()              { SwingPressed(); }
void AEmberwildPlayer::OnBag()                { ToggleBag(); }
void AEmberwildPlayer::OnGear()               { ToggleGear(); }
void AEmberwildPlayer::OnJournal()            { ToggleJournal(); }
void AEmberwildPlayer::OnCreatures()          { ToggleCreatures(); }
void AEmberwildPlayer::OnWeapons()            { ToggleWeapons(); }
void AEmberwildPlayer::OnBack()               { CloseTop(); }

void AEmberwildPlayer::InteractPressed()
{
	if (auto* Sub = Emberwild())
	{
		// with a panel up, E belongs to the menu (advance dialogue / confirm)
		if (Sub->UIRouter.IsValid() && !Sub->UIRouter->IsWorldInputAllowed())
		{
			Sub->UIRouter->HandlePanelKey(EKeys::E);
			return;
		}
		if (!Sub->InteractAllowed()) return;
		AInteractableActor* Best = nullptr;
		float BestDist = InteractRange * InteractRange;
		for (TActorIterator<AInteractableActor> It(GetWorld()); It; ++It)
		{
			const float D = FVector::DistSquared2D(GetActorLocation(), It->GetActorLocation());
			if (D < BestDist) { BestDist = D; Best = *It; }
		}
		if (Best) Best->Interact(this);
	}
}

void AEmberwildPlayer::SwingPressed()
{
	if (CooldownTimer > 0.f) return;
	if (auto* Sub = Emberwild())
	{
		if (Sub->UIRouter.IsValid() && !Sub->UIRouter->IsWorldInputAllowed())
		{
			Sub->UIRouter->HandlePanelKey(EKeys::SpaceBar);
			return;
		}
		const FGameStateSave& Save = Sub->Save;
		CooldownTimer = FPlayerCombat::CooldownOf(Save);
		SwingTimer = FPlayerCombat::SwingSecondsOf(Save);

		// sweep: hit the closest target in reach
		const float Reach = FPlayerCombat::ReachOf(Save);
		ACreatureActor* Best = nullptr;
		float BestDist = Reach * Reach;
		for (TActorIterator<ACreatureActor> It(GetWorld()); It; ++It)
		{
			const float D = FVector::DistSquared2D(GetActorLocation(), It->GetActorLocation());
			if (D < BestDist) { BestDist = D; Best = *It; }
		}
		if (Best)
		{
			const int32 Dmg = FPlayerCombat::DamageRoll(Save);
			Best->TakeHit(Dmg);
			if (auto* Sfx = EmberwildSfx::Get()) Sfx->PlayNamed(TEXT("hit"));
			if (Best->HitPoints <= 0 && !Best->bDefeated)
			{
				Best->bDefeated = true;
				FPlayerCombat::AddXp(Sub->Save, FPlayerCombat::XpPerKO);
				Sub->AwardMeleeKO(Best->SpeciesId, Best->Level);
				Best->Destroy();
			}
			else
			{
				FPlayerCombat::AddXp(Sub->Save, FPlayerCombat::XpPerHit);
			}
		}
	}
}

void AEmberwildPlayer::ToggleBag()      { if (auto* S = Emberwild()) if (S->UIRouter) S->UIRouter->TogglePanel(TEXT("bag")); }
void AEmberwildPlayer::ToggleGear()     { if (auto* S = Emberwild()) if (S->UIRouter) S->UIRouter->TogglePanel(TEXT("gear")); }
void AEmberwildPlayer::ToggleJournal()  { if (auto* S = Emberwild()) if (S->UIRouter) S->UIRouter->TogglePanel(TEXT("journal")); }
void AEmberwildPlayer::ToggleCreatures(){ if (auto* S = Emberwild()) if (S->UIRouter) S->UIRouter->TogglePanel(TEXT("party")); }
void AEmberwildPlayer::ToggleWeapons()  { if (auto* S = Emberwild()) if (S->UIRouter) S->UIRouter->TogglePanel(TEXT("weapons")); }
void AEmberwildPlayer::CloseTop()
{
	if (auto* S = Emberwild())
		if (S->UIRouter.IsValid())
			S->UIRouter->HandlePanelKey(EKeys::Escape);
}

// UI menu keys (arrows/enter/etc.) forwarded into the Slate router
void AEmberwildPlayer::OnUIKey(FKey Key)
{
	if (auto* S = Emberwild())
		if (S->UIRouter.IsValid())
			S->UIRouter->HandlePanelKey(Key);
}

// ================================================================ game mode
AEmberwildGameMode::AEmberwildGameMode()
{
	// spawn the default pawn ourselves
	DefaultPawnClass = AEmberwildPlayer::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

AEmberwildGameMode* AEmberwildGameMode::Get(const UObject* WorldContext)
{
	if (UWorld* W = GEngine && WorldContext ? WorldContext->GetWorld() : nullptr)
		return Cast<AEmberwildGameMode>(UGameplayStatics::GetGameMode(W));
	return nullptr;
}

void AEmberwildGameMode::StartPlay()
{
	Super::StartPlay();
	if (auto* Sub = Emberwild())
		Sub->BootWorld();
}

void AEmberwildGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	// battles and UI run through the subsystem; nothing needed here for now
}
