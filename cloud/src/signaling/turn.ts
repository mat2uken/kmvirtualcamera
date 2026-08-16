import { RtcConfigurationDto } from "./types";
import { Env } from "../env";

export async function getRtcConfiguration(env: Env): Promise<RtcConfigurationDto> {
  const defaultIce: RtcConfigurationDto = {
    iceServers: [
      {
        urls: [
          "stun:stun.l.google.com:19302",
          "stun:stun1.l.google.com:19302",
          "stun:stun2.l.google.com:19302",
          "stun:stun3.l.google.com:19302",
          "stun:stun4.l.google.com:19302",
          "stun:stun.cloudflare.com:3478",
          "stun:global.stun.twilio.com:3478"
        ]
      }
    ],
    iceTransportPolicy: "all"
  };

  if (env.ENABLE_TURN === "true" && env.TURN_KEY_ID && env.TURN_KEY_API_TOKEN) {
    try {
      const resp = await fetch(
        `https://rtc.live.cloudflare.com/v1/turn/keys/${env.TURN_KEY_ID}/credentials/generate-ice-servers`,
        {
          method: "POST",
          headers: {
            Authorization: `Bearer ${env.TURN_KEY_API_TOKEN}`,
            "Content-Type": "application/json"
          },
          body: JSON.stringify({ ttl: 600 })
        }
      );

      if (!resp.ok) {
        throw new Error(`TURN credentials API returned status ${resp.status}`);
      }

      const data = (await resp.json()) as { iceServers?: { urls: string | string[]; username?: string; credential?: string }[] };
      if (data && data.iceServers) {
        // Filter out port 53 ICE URLs for Non-Trickle ICE initial version (CF-022)
        const filteredServers = data.iceServers.map(server => {
          const urls = Array.isArray(server.urls) ? server.urls : [server.urls];
          const filteredUrls = urls.filter(u => !u.includes(":53") && !u.includes("port=53"));
          return { ...server, urls: filteredUrls };
        }).filter(s => s.urls.length > 0);

        return {
          iceServers: [...defaultIce.iceServers, ...filteredServers],
          iceTransportPolicy: "all"
        };
      }
    } catch (err) {
      console.error("Failed to generate TURN credentials:", err);
      throw new Error("TURN_CREDENTIALS_FAILED");
    }
  }

  return defaultIce;
}
