import { Hono } from "hono";
import { Env } from "./env";
import { securityHeaders, staticSecurityHeaders } from "./middleware/security";
import { sessions } from "./routes/sessions";
export { SessionDurableObject } from "./durable/session-object";

const app = new Hono<{ Bindings: Env; Variables: { requestId: string } }>();

// Apply security headers to all requests
app.use("*", securityHeaders);

// Health check endpoint
app.get("/v1/health", (c) => {
  return c.json({
    status: "ok",
    version: "1.0.0",
    time: new Date().toISOString()
  });
});

// Mount Sessions router
app.route("/v1/sessions", sessions);

// Fallback for static assets or 404
app.all("*", async (c) => {
  if (c.env.ASSETS) {
    const res = await c.env.ASSETS.fetch(c.req.raw);
    if (res.status < 400 && c.req.path.startsWith("/send")) {
      const newHeaders = new Headers(res.headers);
      newHeaders.set(
        "Content-Security-Policy",
        "default-src 'self'; script-src 'self' 'unsafe-inline'; img-src 'self' blob: data:; media-src 'self' blob:; connect-src 'self' wss:; base-uri 'none'; frame-ancestors 'none'; form-action 'none';"
      );
      newHeaders.set("Permissions-Policy", "camera=(self), microphone=(self), geolocation=()");
      newHeaders.set("Referrer-Policy", "no-referrer");
      newHeaders.set("X-Content-Type-Options", "nosniff");
      return new Response(res.body, {
        status: res.status,
        statusText: res.statusText,
        headers: newHeaders
      });
    }
    return res;
  }
  return c.json({ error: { code: "NOT_FOUND", message: "Route not found.", retryable: false } }, 404);
});

export default app;
