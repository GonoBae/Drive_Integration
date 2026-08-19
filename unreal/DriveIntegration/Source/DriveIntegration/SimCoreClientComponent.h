#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IWebSocket.h"
#include "SimCoreProtocol.h"
#include "SimCoreClientComponent.generated.h"

UCLASS(ClassGroup=(SimCore), meta=(BlueprintSpawnableComponent))
class DRIVEINTEGRATION_API USimCoreClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimCoreClientComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection") FString ServerUrl = TEXT("ws://127.0.0.1:9000");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection") FString SourceId = TEXT("unreal-manual");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Connection") FString MapPackageChecksum = TEXT("unset");
	// Input changes are sent immediately. The periodic message is only a
	// heartbeat and deliberately stays below UE 5.6's default 30 Hz socket
	// service rate so its internal FIFO cannot accumulate latency.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="1.0", ClampMax="30.0")) float CommandRateHz = 20.0f;
	UFUNCTION(BlueprintCallable, Category="SimCore") void Connect();
	UFUNCTION(BlueprintCallable, Category="SimCore") void Disconnect();
	UFUNCTION(BlueprintPure, Category="SimCore") bool IsConnected() const;
	void SetControl(float Throttle, float Brake, float Steering, bool bHandbrake);
	bool GetLatestState(SimCoreProtocol::FVehicleState& OutState, float& OutStateAgeSeconds) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void SendControl();
	void HandleConnected();
	void HandleConnectionError(const FString& Error);
	void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleRawMessage(const void* Data, SIZE_T Size, SIZE_T BytesRemaining);
	TSharedPtr<IWebSocket> Socket;
	SimCoreProtocol::FControlCommand PendingControl;
	SimCoreProtocol::FVehicleState LatestState;
	TArray<uint8> IncomingMessage;
	uint64 OutgoingSequence = 1;
	double LatestStateReceiveTimeSeconds = 0.0;
	double LastStateArrivalTimeSeconds = 0.0;
	double MaxStateIntervalSeconds = 0.0;
	double LatestStateWallAgeMs = 0.0;
	float SendAccumulator = 0.0f;
	float TelemetryAccumulator = 0.0f;
	uint32 ReceivedStateCount = 0;
	uint32 DroppedOutOfOrderStateCount = 0;
	bool bHasState = false;
	bool bConnectionPending = false;
};
