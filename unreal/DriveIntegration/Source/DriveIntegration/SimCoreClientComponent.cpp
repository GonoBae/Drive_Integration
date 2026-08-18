#include "SimCoreClientComponent.h"

#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCoreClient, Log, All);

USimCoreClientComponent::USimCoreClientComponent() { PrimaryComponentTick.bCanEverTick = true; }
void USimCoreClientComponent::BeginPlay() { Super::BeginPlay(); Connect(); }
void USimCoreClientComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) { Disconnect(); Super::EndPlay(EndPlayReason); }

void USimCoreClientComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SendAccumulator += DeltaTime;
	const float Interval = 1.0f / FMath::Max(CommandRateHz, 1.0f);
	if (SendAccumulator >= Interval) { SendAccumulator = FMath::Fmod(SendAccumulator, Interval); SendControl(); }
}

void USimCoreClientComponent::Connect()
{
	if ((Socket.IsValid() && Socket->IsConnected()) || bConnectionPending) return;
	FWebSocketsModule& Module = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	Socket = Module.CreateWebSocket(ServerUrl);
	Socket->OnConnected().AddUObject(this, &USimCoreClientComponent::HandleConnected);
	Socket->OnConnectionError().AddUObject(this, &USimCoreClientComponent::HandleConnectionError);
	Socket->OnClosed().AddUObject(this, &USimCoreClientComponent::HandleClosed);
	Socket->OnRawMessage().AddUObject(this, &USimCoreClientComponent::HandleRawMessage);
	bConnectionPending = true;
	Socket->Connect();
}

void USimCoreClientComponent::Disconnect()
{
	if (!Socket.IsValid()) return;
	Socket->OnConnected().Clear(); Socket->OnConnectionError().Clear(); Socket->OnClosed().Clear(); Socket->OnRawMessage().Clear();
	if (Socket->IsConnected()) Socket->Close(1000, TEXT("Unreal client shutdown"));
	Socket.Reset(); IncomingMessage.Reset(); bConnectionPending = false;
}

bool USimCoreClientComponent::IsConnected() const { return Socket.IsValid() && Socket->IsConnected(); }
void USimCoreClientComponent::SetControl(float Throttle, float Brake, float Steering, bool bHandbrake)
{
	PendingControl.Throttle = FMath::Clamp(Throttle, 0.0f, 1.0f);
	PendingControl.Brake = FMath::Clamp(Brake, 0.0f, 1.0f);
	PendingControl.Steering = FMath::Clamp(Steering, -1.0f, 1.0f);
	PendingControl.bHandbrake = bHandbrake;
}
bool USimCoreClientComponent::GetLatestState(SimCoreProtocol::FVehicleState& OutState) const
{
	if (!bHasState) return false; OutState = LatestState; return true;
}
void USimCoreClientComponent::SendControl()
{
	if (!IsConnected()) return;
	PendingControl.ClientTimeNs = static_cast<uint64>(FPlatformTime::Seconds() * 1'000'000'000.0);
	const TArray<uint8> Message = SimCoreProtocol::SerializeControlEnvelope(PendingControl, OutgoingSequence++, SourceId, MapPackageChecksum);
	Socket->Send(Message.GetData(), Message.Num(), true);
}
void USimCoreClientComponent::HandleConnected() { bConnectionPending = false; UE_LOG(LogSimCoreClient, Log, TEXT("Connected to %s"), *ServerUrl); }
void USimCoreClientComponent::HandleConnectionError(const FString& Error) { bConnectionPending = false; UE_LOG(LogSimCoreClient, Error, TEXT("Connection failed: %s"), *Error); }
void USimCoreClientComponent::HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	bConnectionPending = false;
	UE_LOG(LogSimCoreClient, Warning, TEXT("Connection closed (%d, clean=%s): %s"), StatusCode, bWasClean ? TEXT("true") : TEXT("false"), *Reason);
}
void USimCoreClientComponent::HandleRawMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
{
	if (Size > static_cast<SIZE_T>(MAX_int32) || Size + static_cast<SIZE_T>(IncomingMessage.Num()) > 1024 * 1024)
	{
		UE_LOG(LogSimCoreClient, Error, TEXT("Rejected oversized SimCore message")); IncomingMessage.Reset(); return;
	}
	IncomingMessage.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
	if (BytesRemaining != 0) return;
	SimCoreProtocol::FVehicleState Parsed; FString Error;
	if (SimCoreProtocol::ParseWorldStateEnvelope(IncomingMessage, Parsed, Error)) { LatestState = Parsed; bHasState = true; }
	else { UE_LOG(LogSimCoreClient, Warning, TEXT("Ignored SimCore packet: %s"), *Error); }
	IncomingMessage.Reset();
}
