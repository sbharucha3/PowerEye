import { useEffect, useState } from "react";
import { MockTelemetryService } from "@/lib/mockTelemetryService";
import type { HubSnapshot } from "@/lib/telemetry";

const services = new Map<string, MockTelemetryService>();

export function getDemoTelemetryService(hubId: string) {
  let service = services.get(hubId);
  if (!service) {
    service = new MockTelemetryService(hubId);
    services.set(hubId, service);
  }
  return service;
}

export function useTelemetry(hubId: string) {
  const service = getDemoTelemetryService(hubId);
  const [snapshot, setSnapshot] = useState<HubSnapshot>(service.getSnapshot());

  useEffect(() => {
    service.start();
    const subscription = service.subscribe(setSnapshot);
    return () => {
      subscription.unsubscribe();
    };
  }, [service]);

  useEffect(() => {
    return () => service.stop();
  }, [service]);

  return snapshot;
}
