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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SimCore|Control", meta=(ClampMin="1.0", ClampMax="120.0")) float CommandRateHz = 60.0f;
	UFUNCTION(BlueprintCallable, Category="SimCore") void Connect();
	UFUNCTION(BlueprintCallable, Category="SimCore") void Disconnect();
	UFUNCTION(BlueprintPure, Category="SimCore") bool IsConnected() const;
	void SetControl(float Throttle, float Brake, float Steering, bool bHandbrake);
	bool GetLatestState(SimCoreProtocol::FVehicleState& OutState) const;

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
	float SendAccumulator = 0.0f;
	bool bHasState = false;
	bool bConnectionPending = false;
};
