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

	TelemetryAccumulator += DeltaTime;
	if (TelemetryAccumulator >= 1.0f)
	{
		const double LocalStateAgeMs = bHasState
			? FMath::Max(0.0, FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds) * 1000.0
			: -1.0;
		const double StateRateHz = ReceivedStateCount / FMath::Max(static_cast<double>(TelemetryAccumulator), 0.001);
		UE_LOG(LogSimCoreClient, Log,
			TEXT("State telemetry: rate=%.1fHz sequence=%llu server_age=%.1fms local_age=%.1fms max_gap=%.1fms dropped_old=%u"),
			StateRateHz,
			static_cast<unsigned long long>(LatestState.Sequence),
			LatestStateWallAgeMs,
			LocalStateAgeMs,
			MaxStateIntervalSeconds * 1000.0,
			DroppedOutOfOrderStateCount);
		TelemetryAccumulator = FMath::Fmod(TelemetryAccumulator, 1.0f);
		ReceivedStateCount = 0;
		DroppedOutOfOrderStateCount = 0;
		MaxStateIntervalSeconds = 0.0;
	}
}

void USimCoreClientComponent::Connect()
{
	if ((Socket.IsValid() && Socket->IsConnected()) || bConnectionPending) return;
	IncomingMessage.Reset();
	bHasState = false;
	LatestState = {};
	LatestStateReceiveTimeSeconds = 0.0;
	LastStateArrivalTimeSeconds = 0.0;
	MaxStateIntervalSeconds = 0.0;
	LatestStateWallAgeMs = 0.0;
	TelemetryAccumulator = 0.0f;
	ReceivedStateCount = 0;
	DroppedOutOfOrderStateCount = 0;
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
	const float NewThrottle = FMath::Clamp(Throttle, 0.0f, 1.0f);
	const float NewBrake = FMath::Clamp(Brake, 0.0f, 1.0f);
	const float NewSteering = FMath::Clamp(Steering, -1.0f, 1.0f);
	const bool bChanged = !FMath::IsNearlyEqual(PendingControl.Throttle, NewThrottle)
		|| !FMath::IsNearlyEqual(PendingControl.Brake, NewBrake)
		|| !FMath::IsNearlyEqual(PendingControl.Steering, NewSteering)
		|| PendingControl.bHandbrake != bHandbrake;
	PendingControl.Throttle = NewThrottle;
	PendingControl.Brake = NewBrake;
	PendingControl.Steering = NewSteering;
	PendingControl.bHandbrake = bHandbrake;
	if (bChanged) SendControl();
}
bool USimCoreClientComponent::GetLatestState(SimCoreProtocol::FVehicleState& OutState, float& OutStateAgeSeconds) const
{
	if (!bHasState) return false;
	OutState = LatestState;
	OutStateAgeSeconds = static_cast<float>(FMath::Max(0.0, FPlatformTime::Seconds() - LatestStateReceiveTimeSeconds));
	return true;
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
	if (SimCoreProtocol::ParseWorldStateEnvelope(IncomingMessage, Parsed, Error))
	{
		if (bHasState && Parsed.Sequence <= LatestState.Sequence)
		{
			++DroppedOutOfOrderStateCount;
			IncomingMessage.Reset();
			return;
		}

		const double ArrivalTimeSeconds = FPlatformTime::Seconds();
		if (LastStateArrivalTimeSeconds > 0.0)
		{
			MaxStateIntervalSeconds = FMath::Max(
				MaxStateIntervalSeconds,
				ArrivalTimeSeconds - LastStateArrivalTimeSeconds);
		}
		const FDateTime UnixEpoch(1970, 1, 1);
		const double UnixNowSeconds = (FDateTime::UtcNow() - UnixEpoch).GetTotalSeconds();
		LatestStateWallAgeMs = (UnixNowSeconds - Parsed.Timestamp) * 1000.0;
		LastStateArrivalTimeSeconds = ArrivalTimeSeconds;
		++ReceivedStateCount;
		LatestState = Parsed;
		LatestStateReceiveTimeSeconds = ArrivalTimeSeconds;
		bHasState = true;
	}
	else { UE_LOG(LogSimCoreClient, Warning, TEXT("Ignored SimCore packet: %s"), *Error); }
	IncomingMessage.Reset();
}
