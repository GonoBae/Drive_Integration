#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IWebSocket.h"
#include "SimCoreProtocol.h"
#include "SimCoreClientComponent.generated.h"

enum class ESimCoreConnectionState : uint8
{
	Disconnected,
	Connecting,
	Connected,
	Incompatible,
	WaitingToReconnect,
	Stopping,
};

UCLASS(ClassGroup=(SimCore), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimCoreClientComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection")
	FString ServerUrl = TEXT("ws://127.0.0.1:9000");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection")
	FString SourceId = TEXT("unreal-manual");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection")
	FString MapPackageChecksum = TEXT("unset");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection", meta=(ClampMin="1"))
	uint32 ControlledEntityId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection", meta=(ClampMin="0.1", ClampMax="10.0"))
	float ReconnectDelaySeconds = 0.5f;

	// Dirty input is coalesced latest-wins and rate-limited. The periodic message
	// is only a lease heartbeat and stays below the socket service rate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="1.0", ClampMax="30.0"))
	float CommandRateHz = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="1.0", ClampMax="30.0"))
	float MaxChangedCommandRateHz = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.0", ClampMax="0.25"))
	float InputDeadzone = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="0.0001", ClampMax="0.1"))
	float ControlChangeEpsilon = 0.005f;

	UFUNCTION(BlueprintCallable, Category="SimCore")
	void Connect();

	UFUNCTION(BlueprintCallable, Category="SimCore")
	void Disconnect();

	UFUNCTION(BlueprintPure, Category="SimCore")
	bool IsConnected() const;

	// Steering is already canonical: -1=right, +1=left. Callers convert device
	// or Unreal right-positive axes before entering this protocol boundary.
	void SetControl(float Throttle, float Brake, float Steering, bool bHandbrake);
	bool GetLatestState(SimCoreProtocol::FVehicleState& OutState, float& OutStateAgeSeconds) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	static constexpr int32 MaxIncomingMessageBytes = 1024 * 1024;

	void StartConnectionAttempt();
	void TickConnection();
	void TickControlTransmission(float DeltaTime);
	void TickTelemetry(float DeltaTime);
	void ResetConnectionSession();
	void ResetReceivedState();
	void ResetControlSession();
	void ResetTelemetry();
	void ScheduleReconnect();
	void SetConnectionState(ESimCoreConnectionState NewState);
	bool IsCurrentSocketGeneration(uint64 Generation) const;
	void BindSocketDelegates(uint64 Generation);
	void UnbindSocketDelegates();
	void ReleaseSocket(bool bCloseConnectedSocket, const FString& CloseReason);
	float ApplyInputDeadzone(float Value) const;
	bool HasMeaningfulControlChange() const;
	bool ShouldSendControl() const;
	void SendControl();
	void ApplyConnected(uint64 Generation);
	void ApplyConnectionError(uint64 Generation, const FString& Error);
	void ApplyClosed(uint64 Generation, int32 StatusCode, const FString& Reason, bool bWasClean);
	void ApplyRawMessage(
		uint64 Generation,
		TArray<uint8> Fragment,
		SIZE_T BytesRemaining,
		bool bFragmentTooLarge);

	TSharedPtr<IWebSocket> Socket;
	FDelegateHandle ConnectedDelegateHandle;
	FDelegateHandle ConnectionErrorDelegateHandle;
	FDelegateHandle ClosedDelegateHandle;
	FDelegateHandle RawMessageDelegateHandle;
	SimCoreProtocol::FControlCommand PendingControl;
	SimCoreProtocol::FControlCommand LastSentControl;
	SimCoreProtocol::FVehicleState LatestState;
	TArray<uint8> IncomingMessage;
	FString SessionId;
	uint64 SocketGeneration = 0;
	uint64 OutgoingSequence = 1;
	double LatestStateReceiveTimeSeconds = 0.0;
	double LastStateArrivalTimeSeconds = 0.0;
	double MaxStateIntervalSeconds = 0.0;
	double LatestStateWallAgeMs = 0.0;
	double NextReconnectTimeSeconds = 0.0;
	float SendAccumulator = 0.0f;
	float TelemetryAccumulator = 0.0f;
	uint32 ReceivedStateCount = 0;
	uint32 DroppedOutOfOrderStateCount = 0;
	bool bHasState = false;
	bool bHasSentControl = false;
	bool bControlDirty = true;
	bool bAutoReconnectEnabled = false;
	bool bDiscardIncomingMessage = false;
	ESimCoreConnectionState ConnectionState = ESimCoreConnectionState::Disconnected;
};
